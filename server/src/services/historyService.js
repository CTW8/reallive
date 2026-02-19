const fs = require('fs');
const path = require('path');
const config = require('../config');

const PROJECT_ROOT = path.resolve(__dirname, '..', '..', '..');

function uniquePaths(values) {
  const out = [];
  const seen = new Set();
  for (const raw of values) {
    if (!raw) continue;
    const source = String(raw || '').trim();
    if (!source) continue;
    const p = path.isAbsolute(source)
      ? path.resolve(source)
      : path.resolve(PROJECT_ROOT, source);
    if (seen.has(p)) continue;
    seen.add(p);
    out.push(p);
  }
  return out;
}

const RECORDINGS_ROOTS = (() => {
  const configRoots = Array.isArray(config?.recordings?.roots)
    ? config.recordings.roots
    : [];
  if (configRoots.length) {
    return uniquePaths(configRoots.map((v) => String(v || '').trim()).filter(Boolean));
  }
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
const EPOCH_MS_MIN = 946684800000;   // 2000-01-01
const EPOCH_MS_MAX = 4102444800000;  // 2100-01-01

const cache = new Map();
const relativeAlignStateByStream = new Map();

function ensureNumber(value, fallback) {
  const n = Number(value);
  return Number.isFinite(n) ? n : fallback;
}

function normalizeTimestampMs(value, fallback) {
  const n = ensureNumber(value, null);
  if (!Number.isFinite(n) || n <= 0) return fallback;
  if (n >= EPOCH_MS_MIN && n <= EPOCH_MS_MAX) return Math.floor(n); // ms
  if (n >= EPOCH_MS_MIN / 1000 && n <= EPOCH_MS_MAX / 1000) return Math.floor(n * 1000); // s
  if (n >= EPOCH_MS_MIN * 1000 && n <= EPOCH_MS_MAX * 1000) return Math.floor(n / 1000); // us
  if (n >= EPOCH_MS_MIN * 1000000 && n <= EPOCH_MS_MAX * 1000000) return Math.floor(n / 1000000); // ns
  return fallback;
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

function readEventLog(streamDir, out) {
  const eventFile = path.join(streamDir, 'events.ndjson');
  if (!fs.existsSync(eventFile)) return;
  let content = '';
  try {
    content = fs.readFileSync(eventFile, 'utf8');
  } catch {
    return;
  }
  if (!content) return;

  const lines = content.split(/\r?\n/);
  for (const line of lines) {
    const raw = line.trim();
    if (!raw) continue;
    let parsed = null;
    try {
      parsed = JSON.parse(raw);
    } catch {
      continue;
    }
    const rawTs = ensureNumber(parsed.ts, null);
    if (!Number.isFinite(rawTs)) continue;
    const normalizedTs = normalizeTimestampMs(rawTs, null);
    const type = String(parsed.type || '').toLowerCase();
    if (type !== 'person') continue;
    const bbox = parsed.bbox && typeof parsed.bbox === 'object' ? parsed.bbox : {};
    const event = {
      ts: Number.isFinite(normalizedTs) ? normalizedTs : Math.floor(rawTs),
      type: 'person-detected',
      score: ensureNumber(parsed.score, 0),
      bbox: {
        x: Math.max(0, Math.floor(ensureNumber(bbox.x, 0))),
        y: Math.max(0, Math.floor(ensureNumber(bbox.y, 0))),
        w: Math.max(0, Math.floor(ensureNumber(bbox.w, 0))),
        h: Math.max(0, Math.floor(ensureNumber(bbox.h, 0))),
      },
      _relativeTs: !Number.isFinite(normalizedTs),
    };
    out.push(event);
  }
}

function alignRelativeEventTimestamps(streamKey, events, segments) {
  if (!events.length) return;
  const relativeIndexes = [];
  for (let i = 0; i < events.length; i += 1) {
    const evt = events[i];
    if (evt && evt._relativeTs && Number.isFinite(evt.ts)) {
      relativeIndexes.push(i);
    }
  }
  if (!relativeIndexes.length) {
    relativeAlignStateByStream.delete(String(streamKey || ''));
    for (const evt of events) {
      if (evt && Object.prototype.hasOwnProperty.call(evt, '_relativeTs')) {
        delete evt._relativeTs;
      }
    }
    return;
  }

  if (segments.length) {
    const stateKey = String(streamKey || '');
    const state = relativeAlignStateByStream.get(stateKey) || { offsets: new Map() };
    const latestSegment = segments[segments.length - 1] || null;
    const latestEndMs = Number(latestSegment?.endMs || 0);
    const earliestStartMs = Number(segments[0]?.startMs || 0);
    if (Number.isFinite(latestEndMs) && latestEndMs > 0) {
      let stableAnchorEnd = latestEndMs;
      if (latestSegment?.isOpen) {
        const closedSegment = [...segments].reverse().find((seg) => !seg?.isOpen);
        stableAnchorEnd = Number(closedSegment?.endMs || segments[0]?.startMs || latestEndMs);
      }

      // Split into monotonic blocks, so timestamp resets (after process restart)
      // are handled independently instead of corrupting the latest block.
      const blocks = [];
      let current = [relativeIndexes[0]];
      let prevTs = Number(events[relativeIndexes[0]].ts || 0);
      for (let i = 1; i < relativeIndexes.length; i += 1) {
        const idx = relativeIndexes[i];
        const ts = Number(events[idx].ts || 0);
        if (!Number.isFinite(ts) || ts < prevTs) {
          blocks.push(current);
          current = [idx];
        } else {
          current.push(idx);
        }
        prevTs = ts;
      }
      blocks.push(current);

      let anchorEnd = stableAnchorEnd;
      for (let bi = blocks.length - 1; bi >= 0; bi -= 1) {
        const block = blocks[bi];
        let blockMin = Number.POSITIVE_INFINITY;
        let blockMax = Number.NEGATIVE_INFINITY;
        for (const idx of block) {
          const ts = Number(events[idx].ts || 0);
          if (!Number.isFinite(ts)) continue;
          if (ts < blockMin) blockMin = ts;
          if (ts > blockMax) blockMax = ts;
        }
        if (!Number.isFinite(blockMin) || !Number.isFinite(blockMax) || blockMax <= 0) {
          continue;
        }

        const blockKey = `${block.length}:${Math.floor(blockMin)}:${Math.floor(blockMax)}`;
        let offsetMs = state.offsets.get(blockKey);
        if (!Number.isFinite(offsetMs)) {
          offsetMs = anchorEnd - blockMax;
          state.offsets.set(blockKey, offsetMs);
        }
        for (const idx of block) {
          const raw = Number(events[idx].ts || 0);
          if (!Number.isFinite(raw)) continue;
          events[idx].ts = Math.floor(raw + offsetMs);
        }

        const blockStartAbs = blockMin + offsetMs;
        anchorEnd = Math.max(earliestStartMs, Math.floor(blockStartAbs - 1000));
      }

      const hardMin = earliestStartMs > 0 ? (earliestStartMs - 24 * 3600 * 1000) : Number.NEGATIVE_INFINITY;
      const hardMax = latestEndMs + 5 * 60 * 1000;
      for (const idx of relativeIndexes) {
        const evt = events[idx];
        if (!evt) continue;
        if (!Number.isFinite(evt.ts)) continue;
        if (evt.ts < hardMin) evt.ts = hardMin;
        if (evt.ts > hardMax) evt.ts = hardMax;
      }
    }
    relativeAlignStateByStream.set(stateKey, state);
    if (relativeAlignStateByStream.size > 128) {
      const firstKey = relativeAlignStateByStream.keys().next().value;
      if (firstKey !== undefined) {
        relativeAlignStateByStream.delete(firstKey);
      }
    }
  }
  for (const evt of events) {
    if (evt && Object.prototype.hasOwnProperty.call(evt, '_relativeTs')) {
      delete evt._relativeTs;
    }
  }
}

function buildSegments(streamKey) {
  const segments = [];
  const events = [];
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

    readEventLog(streamDir, events);
  });

  if (!segments.length) {
    alignRelativeEventTimestamps(streamKey, events, segments);
    return { segments: [], events };
  }

  segments.sort((a, b) => a.startMs - b.startMs);
  alignRelativeEventTimestamps(streamKey, events, segments);
  events.sort((a, b) => a.ts - b.ts);
  return { segments, events };
}

function loadSegments(streamKey) {
  const key = String(streamKey || '');
  if (!key) return { segments: [], events: [] };

  const now = Date.now();
  const hit = cache.get(key);
  if (hit && now - hit.at < CACHE_TTL_MS) {
    return {
      segments: hit.segments,
      events: hit.events || [],
    };
  }

  const built = buildSegments(key);
  cache.set(key, {
    at: now,
    segments: built.segments,
    events: built.events,
  });
  return built;
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
  const { segments, events } = loadSegments(streamKey);
  if (!segments.length) {
    return {
      hasHistory: false,
      nowMs: Date.now(),
      totalDurationMs: 0,
      segmentCount: 0,
      timeRange: null,
      eventCount: events.length,
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
    eventCount: events.length,
  };
}

function getTimeline(streamKey, query = {}) {
  const loaded = loadSegments(streamKey);
  const all = loaded.segments;
  if (!all.length) {
    return {
      startMs: null,
      endMs: null,
      ranges: [],
      thumbnails: [],
      segments: [],
      events: [],
      nowMs: Date.now(),
    };
  }

  const minStart = all[0].startMs;
  const maxEnd = all[all.length - 1].endMs;
  const startMs = ensureNumber(query.start, minStart);
  const endMs = ensureNumber(query.end, maxEnd);

  const segments = all.filter((seg) => seg.endMs >= startMs && seg.startMs <= endMs);
  const ranges = mergeRanges(segments);
  const events = (loaded.events || [])
    .filter((evt) => evt.ts >= startMs && evt.ts <= endMs)
    .slice(-1000);

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
    events,
    nowMs: Date.now(),
  };
}

function getPlayback(streamKey, query = {}) {
  const ts = ensureNumber(query.ts, Date.now());
  const segments = loadSegments(streamKey).segments.filter((seg) => seg.playable && seg.url);
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

function getLatestThumbnail(streamKey) {
  const segments = loadSegments(streamKey).segments;
  if (!segments.length) return null;
  for (let i = segments.length - 1; i >= 0; i -= 1) {
    const seg = segments[i];
    if (seg?.thumbnailUrl) return seg.thumbnailUrl;
  }
  return null;
}

module.exports = {
  RECORDINGS_ROOT,
  RECORDINGS_ROOTS,
  getHistoryOverview,
  getTimeline,
  getPlayback,
  getLatestThumbnail,
};
