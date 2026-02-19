const express = require('express');
const { v4: uuidv4 } = require('uuid');
const authMiddleware = require('../middleware/auth');
const Camera = require('../models/camera');
const { getStreamInfo } = require('../services/srsSync');
const { getSeiInfo } = require('../services/seiMonitor');
const { getDeviceState } = require('../services/mqttControlService');
const edgeReplayService = require('../services/edgeReplayService');
const liveDemandService = require('../services/liveDemandService');
const { getHistoryOverview, getTimeline, getPlayback, getLatestThumbnail } = require('../services/historyService');
const config = require('../config');

const router = express.Router();

function normalizeHost(host) {
  const value = String(host || '').split(',')[0].trim();
  if (!value) return 'localhost';
  if (value.startsWith('[')) {
    const end = value.indexOf(']');
    return end >= 0 ? value.slice(1, end) : value.replace(/^\[|\]$/g, '');
  }
  const first = value.split(':')[0];
  return first || value;
}

function applyTemplate(template, data) {
  return String(template || '').replace(/\{(\w+)\}/g, (_, key) => {
    if (data[key] == null) return '';
    return String(data[key]);
  });
}

function buildStreamUrls(req, streamKey) {
  const forwardedHost = req.get('x-forwarded-host');
  const hostHeader = forwardedHost || req.get('host') || req.hostname || 'localhost';
  const host = normalizeHost(hostHeader);
  const httpHost = String(hostHeader || host).split(',')[0].trim() || host;
  const proto = (req.get('x-forwarded-proto') || req.protocol || 'http').split(',')[0].trim();
  const secureProto = proto === 'https' ? 'https' : 'http';
  const templates = config.streamUrls || {};
  const pushTemplate = templates.pushTemplate || 'rtmp://{host}:1935/live/{streamKey}';
  const pullFlvTemplate = templates.pullFlvTemplate || '{proto}://{httpHost}/live/{streamKey}.flv';
  const pullHlsTemplate = templates.pullHlsTemplate || '{proto}://{httpHost}/live/{streamKey}.m3u8';
  const values = {
    host,
    httpHost,
    streamKey,
    proto: secureProto,
  };
  return {
    push: applyTemplate(pushTemplate, values),
    pull_flv: applyTemplate(pullFlvTemplate, values),
    pull_hls: applyTemplate(pullHlsTemplate, values),
  };
}

// All camera routes require authentication
router.use(authMiddleware);

// GET /api/cameras
router.get('/', (req, res) => {
  const cameras = Camera.findByUserId(req.user.id);
  const enriched = cameras.map((camera) => {
    const device = getDeviceState(camera.stream_key);
    const runtimeStatus = device
      ? (device.activeLive ? 'streaming' : 'online')
      : null;
    const status = runtimeStatus || camera.status || 'offline';
    const thumbnailUrl = status === 'offline' ? null : getLatestThumbnail(camera.stream_key);
    return {
      ...camera,
      status,
      thumbnailUrl,
      device: device || null,
      stream_urls: buildStreamUrls(req, camera.stream_key),
    };
  });
  res.json(enriched);
});

// POST /api/cameras
router.post('/', (req, res) => {
  const { name, resolution } = req.body;
  if (!name) {
    return res.status(400).json({ error: 'Camera name is required' });
  }

  const streamKey = uuidv4();
  const camera = Camera.create(req.user.id, name, streamKey, resolution);
  res.status(201).json({
    ...camera,
    stream_urls: buildStreamUrls(req, camera.stream_key),
  });
});

// PUT /api/cameras/:id
router.put('/:id', (req, res) => {
  const camera = Camera.findById(req.params.id);
  if (!camera) {
    return res.status(404).json({ error: 'Camera not found' });
  }
  if (camera.user_id !== req.user.id) {
    return res.status(403).json({ error: 'Forbidden' });
  }

  const updated = Camera.update(req.params.id, req.user.id, req.body);
  res.json(updated);
});

// DELETE /api/cameras/:id
router.delete('/:id', (req, res) => {
  const camera = Camera.findById(req.params.id);
  if (!camera) {
    return res.status(404).json({ error: 'Camera not found' });
  }
  if (camera.user_id !== req.user.id) {
    return res.status(403).json({ error: 'Forbidden' });
  }

  Camera.delete(req.params.id, req.user.id);
  res.json({ message: 'Camera deleted' });
});

// GET /api/cameras/:id/stream
router.get('/:id/stream', (req, res) => {
  const camera = Camera.findById(req.params.id);
  if (!camera) {
    console.log(`[Camera API] Camera not found: ${req.params.id}`);
    return res.status(404).json({ error: 'Camera not found' });
  }
  if (camera.user_id !== req.user.id) {
    return res.status(403).json({ error: 'Forbidden' });
  }

  // Get real-time SRS stream info if available
  const srsInfo = getStreamInfo(camera.stream_key);
  const seiInfo = getSeiInfo(camera.stream_key);
  const device = getDeviceState(camera.stream_key);
  const liveDemand = liveDemandService.getCameraDemandState(camera.id);
  const runtimeStatus = device
    ? (device.activeLive ? 'streaming' : 'online')
    : null;
  const effectiveStatus = runtimeStatus || camera.status || 'offline';
  const thumbnailUrl = effectiveStatus === 'offline' ? null : getLatestThumbnail(camera.stream_key);
  if (process.env.CAMERA_STREAM_INFO_LOG === '1') {
    console.log(`[Camera API] Getting stream info for camera ${camera.id}, stream_key=${camera.stream_key}, srs=${JSON.stringify(srsInfo)}`);
  }

  res.json({
    camera: {
      id: camera.id,
      name: camera.name,
      resolution: camera.resolution,
      status: effectiveStatus,
      thumbnailUrl,
    },
    stream_key: camera.stream_key,
    stream_urls: buildStreamUrls(req, camera.stream_key),
    signaling_url: `/ws/signaling`,
    room: `camera-${camera.id}`,
    status: effectiveStatus,
    srs: srsInfo,
    sei: seiInfo,
    device: device || null,
    liveDemand,
  });
});

// POST /api/cameras/:id/watch/start
router.post('/:id/watch/start', async (req, res) => {
  const camera = Camera.findById(req.params.id);
  if (!camera) {
    return res.status(404).json({ error: 'Camera not found' });
  }
  if (camera.user_id !== req.user.id) {
    return res.status(403).json({ error: 'Forbidden' });
  }

  const state = await liveDemandService.startWatchSession(camera, req.user.id);
  res.json(state);
});

// POST /api/cameras/:id/watch/heartbeat
router.post('/:id/watch/heartbeat', async (req, res) => {
  const camera = Camera.findById(req.params.id);
  if (!camera) {
    return res.status(404).json({ error: 'Camera not found' });
  }
  if (camera.user_id !== req.user.id) {
    return res.status(403).json({ error: 'Forbidden' });
  }

  const sessionId = String(req.body?.sessionId || req.body?.session_id || '');
  if (!sessionId) {
    return res.status(400).json({ error: 'sessionId is required' });
  }

  const state = await liveDemandService.heartbeatWatchSession(camera, req.user.id, sessionId);
  if (!state.found) {
    return res.status(404).json({ error: 'Watch session not found', ...state });
  }
  res.json(state);
});

// POST /api/cameras/:id/watch/stop
router.post('/:id/watch/stop', async (req, res) => {
  const camera = Camera.findById(req.params.id);
  if (!camera) {
    return res.status(404).json({ error: 'Camera not found' });
  }
  if (camera.user_id !== req.user.id) {
    return res.status(403).json({ error: 'Forbidden' });
  }

  const sessionId = String(req.body?.sessionId || req.body?.session_id || '');
  if (!sessionId) {
    return res.status(400).json({ error: 'sessionId is required' });
  }

  const state = await liveDemandService.stopWatchSession(camera, req.user.id, sessionId);
  res.json(state);
});

// GET /api/cameras/:id/history/overview
router.get('/:id/history/overview', async (req, res) => {
  const camera = Camera.findById(req.params.id);
  if (!camera) {
    return res.status(404).json({ error: 'Camera not found' });
  }
  if (camera.user_id !== req.user.id) {
    return res.status(403).json({ error: 'Forbidden' });
  }

  let overview = null;
  let source = 'edge';

  const localOverview = getHistoryOverview(camera.stream_key);
  if (localOverview?.hasHistory) {
    overview = localOverview;
    source = 'local';
  } else if (edgeReplayService.isEnabled()) {
    overview = await edgeReplayService.getOverview(camera.stream_key);
  }
  if (!overview) {
    overview = localOverview;
    source = 'local';
  }

  res.json({
    stream_key: camera.stream_key,
    source,
    ...overview,
  });
});

// GET /api/cameras/:id/history/timeline?start=...&end=...
router.get('/:id/history/timeline', async (req, res) => {
  const camera = Camera.findById(req.params.id);
  if (!camera) {
    return res.status(404).json({ error: 'Camera not found' });
  }
  if (camera.user_id !== req.user.id) {
    return res.status(403).json({ error: 'Forbidden' });
  }

  let timeline = null;
  let source = 'edge';

  const localTimeline = getTimeline(camera.stream_key, req.query || {});
  if ((localTimeline?.segments || []).length > 0) {
    timeline = localTimeline;
    source = 'local';
  } else if (edgeReplayService.isEnabled()) {
    timeline = await edgeReplayService.getTimeline(camera.stream_key, req.query || {});
  }
  if (!timeline) {
    timeline = localTimeline;
    source = 'local';
  }

  res.json({
    stream_key: camera.stream_key,
    source,
    ...timeline,
  });
});

// GET /api/cameras/:id/history/play?ts=...
router.get('/:id/history/play', async (req, res) => {
  const camera = Camera.findById(req.params.id);
  if (!camera) {
    return res.status(404).json({ error: 'Camera not found' });
  }
  if (camera.user_id !== req.user.id) {
    return res.status(403).json({ error: 'Forbidden' });
  }

  const requestedTs = Number(req.query?.ts);
  const forceMode = String(req.query?.mode || req.query?.source || '').toLowerCase();
  const preferEdge = forceMode === 'edge' || forceMode === 'replay';
  const localPlayback = getPlayback(camera.stream_key, req.query || {});

  let playback = null;
  let source = 'local';

  const localReady = localPlayback &&
    localPlayback.mode === 'history' &&
    !!localPlayback.playbackUrl;

  if (!preferEdge && localReady) {
    playback = localPlayback;
    source = 'local';
  } else if (edgeReplayService.isEnabled()) {
    playback = await edgeReplayService.startReplay(
      camera.stream_key,
      Number.isFinite(requestedTs) ? requestedTs : Date.now()
    );
    if (playback) {
      source = 'edge';
    }
  }

  if (!playback) {
    playback = localPlayback;
    source = 'local';
  }

  res.json({
    stream_key: camera.stream_key,
    source,
    ...playback,
  });
});

// POST /api/cameras/:id/history/replay/stop
router.post('/:id/history/replay/stop', async (req, res) => {
  const camera = Camera.findById(req.params.id);
  if (!camera) {
    return res.status(404).json({ error: 'Camera not found' });
  }
  if (camera.user_id !== req.user.id) {
    return res.status(403).json({ error: 'Forbidden' });
  }

  if (!edgeReplayService.isEnabled()) {
    return res.json({ ok: true, stopped: false, reason: 'edge replay disabled' });
  }

  const result = await edgeReplayService.stopReplay(
    camera.stream_key,
    req.body?.sessionId || null
  );
  if (!result) {
    return res.json({ ok: true, stopped: false });
  }
  return res.json({ ok: true, stopped: true });
});

module.exports = router;
