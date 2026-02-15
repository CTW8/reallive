const config = require('../config');

const EDGE_REPLAY_URL = String(config.edgeReplay?.url || '').trim();
const EDGE_REPLAY_TIMEOUT_MS = Math.max(300, Number(config.edgeReplay?.timeoutMs || 1500));

function isEnabled() {
  return EDGE_REPLAY_URL.length > 0;
}

function toNum(value, fallback = null) {
  const n = Number(value);
  return Number.isFinite(n) ? n : fallback;
}

function toBool(value, fallback = false) {
  if (typeof value === 'boolean') return value;
  if (typeof value === 'number') return value !== 0;
  if (typeof value === 'string') {
    const s = value.toLowerCase();
    if (s === 'true' || s === '1') return true;
    if (s === 'false' || s === '0') return false;
  }
  return fallback;
}

function asTimeRange(value) {
  if (!value || typeof value !== 'object') return null;
  const startMs = toNum(value.startMs ?? value.start_ms, null);
  const endMs = toNum(value.endMs ?? value.end_ms, null);
  if (startMs == null || endMs == null) return null;
  return { startMs, endMs };
}

async function edgeRequest(method, path, body) {
  if (!isEnabled()) return null;

  const controller = new AbortController();
  const timer = setTimeout(() => controller.abort(), EDGE_REPLAY_TIMEOUT_MS);
  try {
    const res = await fetch(`${EDGE_REPLAY_URL}${path}`, {
      method,
      headers: { 'Content-Type': 'application/json' },
      body: body == null ? undefined : JSON.stringify(body),
      signal: controller.signal,
    });

    if (!res.ok) {
      return null;
    }

    const contentType = res.headers.get('content-type') || '';
    if (!contentType.includes('application/json')) {
      return null;
    }
    const json = await res.json();
    return json && typeof json === 'object' ? json : null;
  } catch {
    return null;
  } finally {
    clearTimeout(timer);
  }
}

function normalizeOverview(raw) {
  if (!raw || typeof raw !== 'object') return null;
  const hasHistory = toBool(raw.hasHistory ?? raw.has_history, false);
  const timeRange = asTimeRange(raw.timeRange ?? raw.time_range);

  return {
    hasHistory,
    nowMs: toNum(raw.nowMs ?? raw.now_ms, Date.now()),
    totalDurationMs: toNum(raw.totalDurationMs ?? raw.total_duration_ms, 0),
    segmentCount: toNum(raw.segmentCount ?? raw.segment_count, 0),
    eventCount: toNum(raw.eventCount ?? raw.event_count, 0),
    timeRange,
    ranges: Array.isArray(raw.ranges)
      ? raw.ranges
        .map(asTimeRange)
        .filter(Boolean)
      : [],
  };
}

function normalizeTimeline(raw) {
  if (!raw || typeof raw !== 'object') return null;

  const thumbnails = Array.isArray(raw.thumbnails)
    ? raw.thumbnails.map((item) => ({
      ts: toNum(item.ts ?? item.timestamp_ms, null),
      url: item.url || item.thumbnailUrl || item.thumbnail_url || null,
    })).filter((v) => v.ts != null && v.url)
    : [];

  const segments = Array.isArray(raw.segments)
    ? raw.segments.map((seg) => ({
      id: String(seg.id ?? ''),
      startMs: toNum(seg.startMs ?? seg.start_ms, null),
      endMs: toNum(seg.endMs ?? seg.end_ms, null),
      durationMs: toNum(seg.durationMs ?? seg.duration_ms, null),
      url: seg.url || null,
      thumbnailUrl: seg.thumbnailUrl || seg.thumbnail_url || null,
      playable: toBool(seg.playable, !!seg.url),
      isOpen: toBool(seg.isOpen ?? seg.is_open, false),
      isActive: toBool(seg.isActive ?? seg.is_active, false),
    })).filter((seg) => seg.startMs != null && seg.endMs != null)
    : [];

  const events = Array.isArray(raw.events)
    ? raw.events
      .map((evt) => {
        const bboxRaw = evt?.bbox && typeof evt.bbox === 'object' ? evt.bbox : {};
        return {
          type: String(evt.type || evt.eventType || ''),
          ts: toNum(evt.ts ?? evt.timestamp ?? evt.timestamp_ms, null),
          score: toNum(evt.score, 0),
          bbox: {
            x: toNum(bboxRaw.x, 0),
            y: toNum(bboxRaw.y, 0),
            w: toNum(bboxRaw.w, 0),
            h: toNum(bboxRaw.h, 0),
          },
        };
      })
      .filter((evt) => evt.ts != null)
    : [];

  return {
    startMs: toNum(raw.startMs ?? raw.start_ms, null),
    endMs: toNum(raw.endMs ?? raw.end_ms, null),
    ranges: Array.isArray(raw.ranges)
      ? raw.ranges.map(asTimeRange).filter(Boolean)
      : [],
    thumbnails,
    segments,
    events,
    nowMs: toNum(raw.nowMs ?? raw.now_ms, Date.now()),
  };
}

function inferTransport(url, explicit) {
  if (explicit) return explicit;
  if (!url || typeof url !== 'string') return 'file';
  if (url.includes('.flv')) return 'flv-live';
  return 'file';
}

function normalizePlayback(raw) {
  if (!raw || typeof raw !== 'object') return null;
  const playbackUrl = raw.playbackUrl || raw.playback_url || raw.flvUrl || raw.flv_url || raw.url || null;
  if (!playbackUrl) return null;
  return {
    mode: raw.mode || 'history',
    requestedTs: toNum(raw.requestedTs ?? raw.requested_ts, Date.now()),
    playbackUrl,
    offsetSec: toNum(raw.offsetSec ?? raw.offset_sec, 0),
    sessionId: raw.sessionId || raw.session_id || null,
    transport: inferTransport(playbackUrl, raw.transport),
    segment: raw.segment || null,
  };
}

async function getOverview(streamKey) {
  const response = await edgeRequest('GET', `/api/record/overview?stream_key=${encodeURIComponent(streamKey)}`);
  return normalizeOverview(response);
}

async function getTimeline(streamKey, query = {}) {
  const qs = new URLSearchParams();
  qs.set('stream_key', streamKey);
  if (query.start != null) qs.set('start', String(query.start));
  if (query.end != null) qs.set('end', String(query.end));

  const response = await edgeRequest('GET', `/api/record/timeline?${qs.toString()}`);
  return normalizeTimeline(response);
}

async function startReplay(streamKey, tsMs) {
  const response = await edgeRequest('POST', '/api/record/replay/start', {
    stream_key: streamKey,
    ts: toNum(tsMs, Date.now()),
  });
  return normalizePlayback(response);
}

async function stopReplay(streamKey, sessionId) {
  const response = await edgeRequest('POST', '/api/record/replay/stop', {
    stream_key: streamKey,
    session_id: sessionId || null,
  });
  if (!response || typeof response !== 'object') return null;
  return { ok: true, response };
}

module.exports = {
  isEnabled,
  getOverview,
  getTimeline,
  startReplay,
  stopReplay,
};
