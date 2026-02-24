const express = require('express');
const { v4: uuidv4 } = require('uuid');
const crypto = require('crypto');
const authMiddleware = require('../middleware/auth');
const Camera = require('../models/camera');
const CameraSettings = require('../models/camera-settings');
const { getStreamInfo } = require('../services/srsSync');
const { getSeiInfo } = require('../services/seiMonitor');
const mqttControlService = require('../services/mqttControlService');
const { getDeviceState } = mqttControlService;
const edgeReplayService = require('../services/edgeReplayService');
const liveDemandService = require('../services/liveDemandService');
const { getHistoryOverview, getTimeline, getPlayback, getLatestThumbnail } = require('../services/historyService');
const config = require('../config');

const router = express.Router();
const CAMERA_STREAM_API_BUILD_TAG = 'camera-stream-v6';

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

function bumpFirmwareVersion(version) {
  const m = String(version || '').match(/^v?(\d+)\.(\d+)\.(\d+)$/i);
  if (!m) return 'v2.3.9';
  const major = Number(m[1]) || 0;
  const minor = Number(m[2]) || 0;
  const patch = (Number(m[3]) || 0) + 1;
  return `v${major}.${minor}.${patch}`;
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

function levelTarget(level) {
  const safe = Math.max(0, Math.min(4, Number(level) || 0));
  switch (safe) {
    case 0: return { fps: 12, kbps: 350 };
    case 1: return { fps: 15, kbps: 600 };
    case 2: return { fps: 20, kbps: 1200 };
    case 3: return { fps: 25, kbps: 1800 };
    default: return { fps: 30, kbps: 2500 };
  }
}

function encodeBase64Url(input) {
  return Buffer.from(input).toString('base64url');
}

function buildShareToken(payload) {
  const body = encodeBase64Url(JSON.stringify(payload));
  const sig = crypto
    .createHmac('sha256', config.jwtSecret || 'reallive-share')
    .update(body)
    .digest('base64url');
  return `${body}.${sig}`;
}

function clampTtlSec(ttlSec) {
  const min = Math.max(1, Number(config?.shareLinks?.minTtlSec || 60));
  const max = Math.max(min, Number(config?.shareLinks?.maxTtlSec || 30 * 24 * 60 * 60));
  const safe = Number.isFinite(Number(ttlSec)) ? Number(ttlSec) : 0;
  if (!safe || safe < min) return min;
  if (safe > max) return max;
  return Math.floor(safe);
}

function buildEffectiveProfile(settings, device) {
  const profileOption = String(device?.streamProfile || device?.stream_profile || settings?.stream_profile || 'auto').toLowerCase();
  if (device) {
    const mode = String(device.streamMode || device.stream_mode || settings?.stream_mode || 'auto').toLowerCase();
    const level = Math.max(0, Math.min(4, Number(device.profileLevel ?? device.profile_level ?? settings?.manual_level ?? 2) || 2));
    const fps = Math.max(1, Number(device.targetFps ?? device.target_fps ?? levelTarget(level).fps) || levelTarget(level).fps);
    const kbps = Math.max(100, Number(device.targetBitrateKbps ?? device.target_bitrate_kbps ?? levelTarget(level).kbps) || levelTarget(level).kbps);
    return {
      source: 'device',
      profileOption,
      mode,
      level,
      targetFps: fps,
      targetBitrateKbps: kbps,
      autoPolicy: String(device.autoPolicy || device.auto_policy || settings?.auto_policy || 'balanced'),
      autoMinLevel: Math.max(0, Math.min(4, Number(device.autoMinLevel ?? device.auto_min_level ?? settings?.auto_min_level ?? 0) || 0)),
      autoMaxLevel: Math.max(0, Math.min(4, Number(device.autoMaxLevel ?? device.auto_max_level ?? settings?.auto_max_level ?? 4) || 4)),
    };
  }
  const mode = String(settings?.stream_mode || 'auto').toLowerCase();
  const level = mode === 'manual'
    ? Math.max(0, Math.min(4, Number(settings?.manual_level ?? 2) || 2))
    : Math.max(0, Math.min(4, Number(settings?.auto_min_level ?? 0) || 0));
  const target = levelTarget(level);
  return {
    source: 'settings',
    profileOption,
    mode,
    level,
    targetFps: target.fps,
    targetBitrateKbps: target.kbps,
    autoPolicy: String(settings?.auto_policy || 'balanced'),
    autoMinLevel: Math.max(0, Math.min(4, Number(settings?.auto_min_level ?? 0) || 0)),
    autoMaxLevel: Math.max(0, Math.min(4, Number(settings?.auto_max_level ?? 4) || 4)),
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

  const streamKeyRaw = String(req.body?.streamKey || '').trim();
  const streamKey = streamKeyRaw || uuidv4();
  if (!/^[a-zA-Z0-9._-]{8,128}$/.test(streamKey)) {
    return res.status(400).json({ error: 'Invalid streamKey format' });
  }
  const exists = Camera.findByStreamKey(streamKey);
  if (exists) {
    if (Number(exists.user_id) === Number(req.user.id)) {
      return res.status(409).json({ error: 'Stream key already exists in your account', cameraId: exists.id });
    }
    return res.status(409).json({ error: 'Stream key already in use' });
  }
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
  const cameraSettings = CameraSettings.getByCameraId(camera.id);
  const device = getDeviceState(camera.stream_key);
  const effectiveProfile = buildEffectiveProfile(cameraSettings, device);
  const liveDemand = liveDemandService.getCameraDemandState(camera.id);
  const runtimeStatus = device
    ? (device.activeLive ? 'streaming' : 'online')
    : null;
  const effectiveStatus = runtimeStatus || camera.status || 'offline';
  const thumbnailUrl = effectiveStatus === 'offline' ? null : getLatestThumbnail(camera.stream_key);
  if (process.env.CAMERA_STREAM_INFO_LOG !== '0') {
    const seiTelemetry = seiInfo?.telemetry || null;
    console.log(
      `[Camera API] stream camera=${camera.id} key=${camera.stream_key} ` +
      `srsFps=${srsInfo?.fps ?? 'null'} srsRecv=${srsInfo?.kbps?.recv_30s ?? 'null'} srsSend=${srsInfo?.kbps?.send_30s ?? 'null'} ` +
      `seiFps=${seiTelemetry?.streamOutFps ?? 'null'} seiKbps=${seiTelemetry?.streamOutBitrateKbps ?? 'null'} seiUpdated=${seiInfo?.updatedAt ?? 'null'}`
    );
  }

  res.json({
    serverBuildTag: CAMERA_STREAM_API_BUILD_TAG,
    camera: {
      id: camera.id,
      name: camera.name,
      location: camera.location || '',
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
    camera_settings: cameraSettings,
    device: device || null,
    effective_profile: effectiveProfile,
    liveDemand,
  });
});

// POST /api/cameras/:id/share-link
router.post('/:id/share-link', (req, res) => {
  const camera = Camera.findById(req.params.id);
  if (!camera) {
    return res.status(404).json({ error: 'Camera not found' });
  }
  if (camera.user_id !== req.user.id) {
    return res.status(403).json({ error: 'Forbidden' });
  }

  const modeRaw = String(req.body?.mode || 'view').trim().toLowerCase();
  const mode = modeRaw === '24h' ? '24h' : 'view';
  const oneTime = !!req.body?.oneTime;
  const requestedTtl = Number(req.body?.ttlSec || 0);
  const now = Date.now();
  const defaultTtl = mode === '24h'
    ? Number(config?.shareLinks?.default24hTtlSec || 24 * 60 * 60)
    : Number(config?.shareLinks?.defaultViewTtlSec || 7 * 24 * 60 * 60);
  const ttlSec = clampTtlSec(requestedTtl > 0 ? requestedTtl : defaultTtl);
  const expiresAt = now + ttlSec * 1000;
  const jti = uuidv4();
  const token = buildShareToken({
    v: 1,
    uid: req.user.id,
    cid: camera.id,
    stream_key: camera.stream_key,
    mode,
    one_time: oneTime,
    jti,
    exp: expiresAt,
    iat: now,
  });
  const hostHeader = req.get('x-forwarded-host') || req.get('host') || req.hostname || 'localhost';
  const proto = (req.get('x-forwarded-proto') || req.protocol || 'http').split(',')[0].trim();
  const safeProto = proto === 'https' ? 'https' : 'http';
  const flvUrl = `${safeProto}://${hostHeader}/share/live/${camera.stream_key}.flv?st=${encodeURIComponent(token)}`;

  return res.json({
    ok: true,
    mode,
    oneTime,
    ttlSec,
    cameraId: camera.id,
    streamKey: camera.stream_key,
    expiresAt,
    jti,
    token,
    url: flvUrl,
  });
});

// GET /api/cameras/:id/settings
router.get('/:id/settings', (req, res) => {
  const camera = Camera.findById(req.params.id);
  if (!camera) {
    return res.status(404).json({ error: 'Camera not found' });
  }
  if (camera.user_id !== req.user.id) {
    return res.status(403).json({ error: 'Forbidden' });
  }
  const settings = CameraSettings.getByCameraId(camera.id);
  let deviceApply = null;
  if (mqttControlService.isEnabled()) {
    const published = mqttControlService.publishCameraSettingsCommand(camera.stream_key, settings);
    deviceApply = {
      mqttEnabled: true,
      mqttReady: mqttControlService.isReady(),
      published,
    };
  }
  const device = getDeviceState(camera.stream_key);
  const effectiveProfile = buildEffectiveProfile(settings, device);
  const runtimeStatus = device
    ? (device.activeLive ? 'streaming' : 'online')
    : null;
  const status = runtimeStatus || camera.status || 'offline';
  return res.json({
    id: camera.id,
    name: camera.name,
    resolution: camera.resolution || '1080p',
    location: camera.location || settings.location || '',
    status,
    settings,
    device: device || null,
    effective_profile: effectiveProfile,
    deviceApply,
  });
});

// PUT /api/cameras/:id/settings
router.put('/:id/settings', (req, res) => {
  const camera = Camera.findById(req.params.id);
  if (!camera) {
    return res.status(404).json({ error: 'Camera not found' });
  }
  if (camera.user_id !== req.user.id) {
    return res.status(403).json({ error: 'Forbidden' });
  }

  const cameraPatch = {};
  if (req.body?.name != null) cameraPatch.name = String(req.body.name).trim();
  if (req.body?.resolution != null) cameraPatch.resolution = String(req.body.resolution).trim();
  if (req.body?.location != null) cameraPatch.location = String(req.body.location).trim();
  if (cameraPatch.name === '') {
    return res.status(400).json({ error: 'Camera name cannot be empty' });
  }

  const settingsPatch = req.body?.settings || req.body || {};
  if (settingsPatch.stream_profile != null) {
    const profile = String(settingsPatch.stream_profile).trim().toLowerCase();
    if (!['auto', '360p', '540p', '720p', '1080p'].includes(profile)) {
      return res.status(400).json({ error: 'stream_profile must be one of auto/360p/540p/720p/1080p' });
    }
  }
  if (settingsPatch.stream_mode != null) {
    const mode = String(settingsPatch.stream_mode).trim().toLowerCase();
    if (mode !== 'manual' && mode !== 'auto') {
      return res.status(400).json({ error: 'stream_mode must be manual or auto' });
    }
  }
  if (settingsPatch.auto_policy != null) {
    const policy = String(settingsPatch.auto_policy).trim().toLowerCase();
    if (!['stable', 'balanced', 'quality'].includes(policy)) {
      return res.status(400).json({ error: 'auto_policy must be stable, balanced, or quality' });
    }
  }
  const numberKeys = [
    'manual_level',
    'auto_min_level',
    'auto_max_level',
    'auto_cooldown_sec',
    'auto_up_hold_sec',
    'auto_down_hold_sec',
  ];
  for (const key of numberKeys) {
    if (settingsPatch[key] == null) continue;
    const n = Number(settingsPatch[key]);
    if (!Number.isFinite(n)) {
      return res.status(400).json({ error: `${key} must be a number` });
    }
  }
  if (settingsPatch.auto_min_level != null || settingsPatch.auto_max_level != null) {
    const min = Number(settingsPatch.auto_min_level ?? 0);
    const max = Number(settingsPatch.auto_max_level ?? 4);
    if (Number.isFinite(min) && Number.isFinite(max) && min > max) {
      return res.status(400).json({ error: 'auto_min_level must be <= auto_max_level' });
    }
  }

  const updatedCamera = Camera.update(camera.id, req.user.id, cameraPatch);
  const settings = CameraSettings.upsert(camera.id, settingsPatch);
  let deviceApply = null;
  if (mqttControlService.isEnabled()) {
    const published = mqttControlService.publishCameraSettingsCommand(updatedCamera.stream_key, settings);
    deviceApply = {
      mqttEnabled: true,
      mqttReady: mqttControlService.isReady(),
      published,
    };
  }
  const device = getDeviceState(updatedCamera.stream_key);
  const effectiveProfile = buildEffectiveProfile(settings, device);
  const runtimeStatus = device
    ? (device.activeLive ? 'streaming' : 'online')
    : null;
  const status = runtimeStatus || updatedCamera.status || 'offline';
  return res.json({
    id: updatedCamera.id,
    name: updatedCamera.name,
    resolution: updatedCamera.resolution || '1080p',
    location: updatedCamera.location || settings.location || '',
    status,
    settings,
    device: device || null,
    effective_profile: effectiveProfile,
    deviceApply,
  });
});

// GET /api/cameras/:id/network
router.get('/:id/network', (req, res) => {
  const camera = Camera.findById(req.params.id);
  if (!camera) {
    return res.status(404).json({ error: 'Camera not found' });
  }
  if (camera.user_id !== req.user.id) {
    return res.status(403).json({ error: 'Forbidden' });
  }
  const device = getDeviceState(camera.stream_key);
  const suffix = String(camera.stream_key || '').slice(0, 6);
  const connected = camera.status !== 'offline' || !!device;
  return res.json({
    cameraId: camera.id,
    connected,
    ssid: `camera-${suffix}`,
    signal: device?.activeLive ? 'Excellent' : (connected ? 'Good' : 'Disconnected'),
    ip: camera.ip_address || null,
    model: camera.model || 'RealLive Cam',
  });
});

// POST /api/cameras/:id/firmware/update
router.post('/:id/firmware/update', (req, res) => {
  const camera = Camera.findById(req.params.id);
  if (!camera) {
    return res.status(404).json({ error: 'Camera not found' });
  }
  if (camera.user_id !== req.user.id) {
    return res.status(403).json({ error: 'Forbidden' });
  }
  const settings = CameraSettings.getByCameraId(camera.id);
  const nextVersion = bumpFirmwareVersion(settings.firmware_version);
  const next = CameraSettings.upsert(camera.id, {
    firmware_version: nextVersion,
    firmware_update_available: 0,
  });
  return res.json({
    ok: true,
    cameraId: camera.id,
    firmwareVersion: next.firmware_version,
    firmwareUpdateAvailable: next.firmware_update_available,
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

// POST /api/cameras/:id/ptz
router.post('/:id/ptz', (req, res) => {
  const camera = Camera.findById(req.params.id);
  if (!camera) {
    return res.status(404).json({ error: 'Camera not found' });
  }
  if (camera.user_id !== req.user.id) {
    return res.status(403).json({ error: 'Forbidden' });
  }

  const action = String(req.body?.action || req.body?.command || '').trim().toLowerCase();
  const validActions = new Set([
    'up',
    'down',
    'left',
    'right',
    'up_left',
    'up_right',
    'down_left',
    'down_right',
    'stop',
    'home',
    'zoom_in',
    'zoom_out',
    'zoom_set',
    'preset',
  ]);
  if (!validActions.has(action)) {
    return res.status(400).json({ error: 'Invalid PTZ action' });
  }

  const speedRaw = req.body?.speed;
  const speed = speedRaw == null ? 5 : Number(speedRaw);
  if (!Number.isFinite(speed)) {
    return res.status(400).json({ error: 'speed must be a number' });
  }
  const safeSpeed = Math.max(1, Math.min(10, Math.round(speed)));

  let zoomLevel = null;
  if (req.body?.zoom_level != null) {
    const n = Number(req.body.zoom_level);
    if (!Number.isFinite(n)) {
      return res.status(400).json({ error: 'zoom_level must be a number' });
    }
    zoomLevel = Math.max(0, Math.min(100, Math.round(n)));
  }

  const zoomStepRaw = req.body?.zoom_step;
  const zoomStep = zoomStepRaw == null ? 1 : Number(zoomStepRaw);
  if (!Number.isFinite(zoomStep)) {
    return res.status(400).json({ error: 'zoom_step must be a number' });
  }
  const safeZoomStep = Math.max(1, Math.min(10, Math.round(zoomStep)));

  const preset = req.body?.preset != null ? String(req.body.preset).trim() : null;
  const published = mqttControlService.publishPtzCommand(camera.stream_key, {
    action,
    speed: safeSpeed,
    zoom_step: safeZoomStep,
    zoom_level: zoomLevel,
    preset,
  });
  if (mqttControlService.isEnabled() && !published) {
    return res.status(503).json({
      error: 'PTZ command publish failed',
      mqttEnabled: true,
      mqttReady: mqttControlService.isReady(),
    });
  }

  return res.json({
    ok: true,
    cameraId: camera.id,
    streamKey: camera.stream_key,
    action,
    speed: safeSpeed,
    zoom_step: safeZoomStep,
    zoom_level: zoomLevel,
    preset,
    mqttEnabled: mqttControlService.isEnabled(),
    mqttReady: mqttControlService.isReady(),
    published,
    device: getDeviceState(camera.stream_key),
  });
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
