const fs = require('fs');
const path = require('path');

const PROJECT_ROOT = path.resolve(__dirname, '..', '..', '..');

function uniquePaths(values) {
  const out = [];
  const seen = new Set();
  for (const raw of values) {
    if (!raw) continue;
    const p = path.resolve(raw);
    if (seen.has(p)) continue;
    seen.add(p);
    out.push(p);
  }
  return out;
}

const RECORDINGS_ROOTS = (() => {
  const raw = process.env.RECORDINGS_ROOT || '';
  if (raw.trim()) {
    return uniquePaths(raw.split(',').map((v) => v.trim()).filter(Boolean));
  }
  return uniquePaths([
    path.join(PROJECT_ROOT, 'recordings'),
    path.join(PROJECT_ROOT, 'pusher', 'recordings'),
    path.join(PROJECT_ROOT, 'pusher', 'build', 'recordings'),
  ]);
})();
const RECORDINGS_ROOT = RECORDINGS_ROOTS[0];
const DEFAULT_SEGMENT_MS = Number(process.env.HISTORY_DEFAULT_SEGMENT_MS || 60000);
const CACHE_TTL_MS = Number(process.env.HISTORY_CACHE_TTL_MS || 2000);

const cache = new Map();

function ensureNumber(value, fallback) {
  const n = Number(value);
  return Number.isFinite(n) ? n : fallback;
}

function isVideoFile(name) {
  return name.endsWith('.mp4') || name.endsWith('.m4v');
}

function isOpenSegmentFile(name) {
  return /segment_(\d+)_open\.writing$/.test(name);
}

function parseOpenSegmentStartMs(filename) {
  const match = filename.match(/segment_(\d+)_open\.writing$/);
  if (!match) return null;
  const n = Number(match[1]);
  return Number.isFinite(n) ? n : null;
}

function parseTimesFromName(filename) {
  const base = filename.replace(/\.[^.]+$/, '');

  let match = base.match(/(\d{13})[_-](\d{13})/);
  if (match) {
    return { startMs: Number(match[1]), endMs: Number(match[2]) };
  }

  match = base.match(/(\d{10})[_-](\d{10})/);
  if (match) {
    return { startMs: Number(match[1]) * 1000, endMs: Number(match[2]) * 1000 };
  }

  match = base.match(/(\d{8})_(\d{6})/);
  if (match) {
    const d = match[1];
    const t = match[2];
    const year = Number(d.slice(0, 4));
    const month = Number(d.slice(4, 6)) - 1;
    const day = Number(d.slice(6, 8));
    const hour = Number(t.slice(0, 2));
    const minute = Number(t.slice(2, 4));
    const second = Number(t.slice(4, 6));
    const startMs = new Date(year, month, day, hour, minute, second).getTime();
    if (Number.isFinite(startMs)) {
      return { startMs, endMs: null };
    }
  }

  return { startMs: null, endMs: null };
}

function safeRel(root, target) {
  const rel = path.relative(root, target).replace(/\\/g, '/');
  if (rel.startsWith('..')) return null;
  return rel;
}

function walkFiles(dir, out) {
  let entries = [];
  try {
    entries = fs.readdirSync(dir, { withFileTypes: true });
  } catch {
    return;
  }

  for (const entry of entries) {
    const fullPath = path.join(dir, entry.name);
    if (entry.isDirectory()) {
      walkFiles(fullPath, out);
      continue;
    }
    if (entry.isFile() && (isVideoFile(entry.name) || isOpenSegmentFile(entry.name))) {
      out.push(fullPath);
    }
  }
}

function buildSegments(streamKey) {
  const segments = [];
  RECORDINGS_ROOTS.forEach((root, rootIndex) => {
    const streamDir = path.join(root, streamKey);
    if (!fs.existsSync(streamDir)) return;

    const files = [];
    walkFiles(streamDir, files);

    for (const filePath of files) {
      let stat;
      try {
        stat = fs.statSync(filePath);
      } catch {
        continue;
      }

      const relPath = safeRel(root, filePath);
      if (!relPath) continue;

      const filename = path.basename(filePath);
      const parsed = parseTimesFromName(filename);
      let startMs = parsed.startMs;
      let endMs = parsed.endMs;
      const isOpen = isOpenSegmentFile(filename);
      let isActive = false;

      if (isOpen) {
        startMs = parseOpenSegmentStartMs(filename) ?? startMs;
        const lastWriteMs = Number(stat.mtimeMs) || Date.now();
        isActive = (Date.now() - lastWriteMs) < 15000;
        endMs = isActive
          ? Date.now()
          : Math.max((startMs || 0) + 1000, lastWriteMs);
      }

      if (!startMs) {
        startMs = stat.mtimeMs - DEFAULT_SEGMENT_MS;
      }
      if (!endMs || endMs <= startMs) {
        endMs = Math.max(startMs + 1000, stat.mtimeMs);
      }

      const thumbCandidate = filePath.replace(/\.[^.]+$/, '.jpg');
      const thumbRel = fs.existsSync(thumbCandidate) ? safeRel(root, thumbCandidate) : null;
      const idRaw = `${rootIndex}:${relPath}`;

      segments.push({
        id: Buffer.from(idRaw).toString('base64url'),
        filename,
        filePath,
        relPath,
        rootIndex,
        url: isOpen ? null : `/history-files/${rootIndex}/${encodeURI(relPath)}`,
        startMs: Math.floor(startMs),
        endMs: Math.floor(endMs),
        durationMs: Math.max(1000, Math.floor(endMs - startMs)),
        sizeBytes: stat.size,
        thumbnailUrl: thumbRel ? `/history-files/${rootIndex}/${encodeURI(thumbRel)}` : null,
        isOpen,
        isActive,
        playable: !isOpen,
      });
    }
  });

  if (!segments.length) {
    return [];
  }

  segments.sort((a, b) => a.startMs - b.startMs);
  return segments;
}

function loadSegments(streamKey) {
  const key = String(streamKey || '');
  if (!key) return [];

  const now = Date.now();
  const hit = cache.get(key);
  if (hit && now - hit.at < CACHE_TTL_MS) {
    return hit.segments;
  }

  const segments = buildSegments(key);
  cache.set(key, { at: now, segments });
  return segments;
}

function mergeRanges(segments) {
  const ranges = [];
  for (const seg of segments) {
    const last = ranges[ranges.length - 1];
    if (!last || seg.startMs > last.endMs + 1000) {
      ranges.push({ startMs: seg.startMs, endMs: seg.endMs });
    } else {
      last.endMs = Math.max(last.endMs, seg.endMs);
    }
  }
  return ranges;
}

function getHistoryOverview(streamKey) {
  const segments = loadSegments(streamKey);
  if (!segments.length) {
    return {
      hasHistory: false,
      nowMs: Date.now(),
      totalDurationMs: 0,
      segmentCount: 0,
      timeRange: null,
    };
  }

  const timeRange = {
    startMs: segments[0].startMs,
    endMs: segments[segments.length - 1].endMs,
  };
  const totalDurationMs = segments.reduce((sum, seg) => sum + seg.durationMs, 0);
  const activeSegment = segments.find((seg) => seg.isOpen && seg.isActive) || null;

  return {
    hasHistory: true,
    nowMs: Date.now(),
    totalDurationMs,
    segmentCount: segments.length,
    timeRange,
    ranges: mergeRanges(segments),
    hasActiveRecording: !!activeSegment,
    activeRecordingStartMs: activeSegment ? activeSegment.startMs : null,
  };
}

function getTimeline(streamKey, query = {}) {
  const all = loadSegments(streamKey);
  if (!all.length) {
    return {
      startMs: null,
      endMs: null,
      ranges: [],
      thumbnails: [],
      segments: [],
      nowMs: Date.now(),
    };
  }

  const minStart = all[0].startMs;
  const maxEnd = all[all.length - 1].endMs;
  const startMs = ensureNumber(query.start, minStart);
  const endMs = ensureNumber(query.end, maxEnd);

  const segments = all.filter((seg) => seg.endMs >= startMs && seg.startMs <= endMs);
  const ranges = mergeRanges(segments);

  const thumbnails = [];
  for (const seg of segments) {
    if (!seg.thumbnailUrl) continue;
    thumbnails.push({
      ts: Math.floor((seg.startMs + seg.endMs) / 2),
      url: seg.thumbnailUrl,
    });
    if (thumbnails.length >= 200) break;
  }

  return {
    startMs,
    endMs,
    ranges,
    thumbnails,
    segments: segments.map((seg) => ({
      id: seg.id,
      startMs: seg.startMs,
      endMs: seg.endMs,
      durationMs: seg.durationMs,
      url: seg.url,
      thumbnailUrl: seg.thumbnailUrl,
      playable: !!seg.playable,
      isOpen: !!seg.isOpen,
      isActive: !!seg.isActive,
    })),
    nowMs: Date.now(),
  };
}

function getPlayback(streamKey, query = {}) {
  const ts = ensureNumber(query.ts, Date.now());
  const segments = loadSegments(streamKey).filter((seg) => seg.playable && seg.url);
  if (!segments.length) {
    return {
      mode: 'live',
      requestedTs: ts,
      playbackUrl: null,
      offsetSec: 0,
      segment: null,
    };
  }

  let target = segments.find((seg) => ts >= seg.startMs && ts <= seg.endMs);
  if (!target) {
    for (let i = segments.length - 1; i >= 0; i -= 1) {
      if (segments[i].startMs <= ts) {
        target = segments[i];
        break;
      }
    }
  }
  if (!target) {
    target = segments[0];
  }

  const offsetSec = Math.max(0, Math.floor((ts - target.startMs) / 1000));
  return {
    mode: 'history',
    requestedTs: ts,
    playbackUrl: target.url,
    offsetSec,
    segment: {
      id: target.id,
      startMs: target.startMs,
      endMs: target.endMs,
      durationMs: target.durationMs,
    },
  };
}

module.exports = {
  RECORDINGS_ROOT,
  RECORDINGS_ROOTS,
  getHistoryOverview,
  getTimeline,
  getPlayback,
};
