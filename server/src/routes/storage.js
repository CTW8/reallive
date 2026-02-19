const express = require('express');
const authMiddleware = require('../middleware/auth');
const Camera = require('../models/camera');
const { getSeiInfo } = require('../services/seiMonitor');
const mqttControlService = require('../services/mqttControlService');

const router = express.Router();
router.use(authMiddleware);

const cloudStorageState = {
  enabled: false,
  provider: 's3',
  bucket: '',
  region: '',
  endpoint: '',
  totalGb: 2000,
  usedGb: 0,
  syncStatus: 'disconnected',
  lastSyncMs: 0,
};

function toNum(value, fallback = 0) {
  const n = Number(value);
  return Number.isFinite(n) ? n : fallback;
}

function clamp(value, min, max) {
  return Math.max(min, Math.min(max, value));
}

function pickPositive(primary, fallback) {
  const p = toNum(primary, 0);
  if (p > 0) return p;
  return Math.max(0, toNum(fallback, 0));
}

function normalizeStorage(telemetry, runtime) {
  if ((!telemetry || typeof telemetry !== 'object') && (!runtime || typeof runtime !== 'object')) {
    return { totalGb: 0, usedGb: 0, freeGb: 0, usedPercent: 0 };
  }
  const totalGb = pickPositive(telemetry?.storageTotalGb, runtime?.storageTotalGb);
  const usedGb = clamp(
    pickPositive(telemetry?.storageUsedGb, runtime?.storageUsedGb),
    0,
    totalGb || Number.MAX_SAFE_INTEGER
  );
  const freeGb = Math.max(0, totalGb - usedGb);
  const usedPercent = totalGb > 0
    ? clamp((usedGb / totalGb) * 100, 0, 100)
    : clamp(toNum(runtime?.storagePct, 0), 0, 100);
  return {
    totalGb: Math.round(totalGb * 100) / 100,
    usedGb: Math.round(usedGb * 100) / 100,
    freeGb: Math.round(freeGb * 100) / 100,
    usedPercent: Math.round(usedPercent * 10) / 10,
  };
}

function collectStorageByDevice(userId, camerasOverride = null) {
  const cameras = Array.isArray(camerasOverride) ? camerasOverride : Camera.findByUserId(userId);
  return cameras.map((camera) => {
    const sei = getSeiInfo(camera.stream_key);
    const telemetry = sei?.telemetry || null;
    const runtime = mqttControlService.getDeviceState(camera.stream_key);
    const storage = normalizeStorage(telemetry, runtime);
    const history = Array.isArray(sei?.telemetryHistory) ? sei.telemetryHistory : [];
    const lastUpdateMs = Math.max(toNum(sei?.updatedAt, 0), toNum(runtime?.updatedAt, 0));
    return {
      id: camera.id,
      cameraId: camera.id,
      streamKey: camera.stream_key,
      name: camera.name,
      status: runtime ? (runtime.activeLive ? 'streaming' : 'online') : (camera.status || 'offline'),
      lastUpdateMs,
      totalGb: storage.totalGb,
      usedGb: storage.usedGb,
      freeGb: storage.freeGb,
      usedPercent: storage.usedPercent,
      minFreePercent: clamp(toNum(runtime?.recordMinFreePercent, 15), 1, 95),
      telemetryHistory: history,
    };
  });
}

function requestStorageRefresh(cameras) {
  if (!mqttControlService.isEnabled() || !mqttControlService.isReady()) return;
  for (const camera of cameras || []) {
    mqttControlService.publishStorageQueryCommand(camera.stream_key);
  }
}

function aggregateOverview(devices) {
  const total = devices.reduce((sum, d) => sum + d.totalGb, 0);
  const used = devices.reduce((sum, d) => sum + d.usedGb, 0);
  const free = Math.max(0, total - used);
  const usedPercent = total > 0 ? (used / total) * 100 : 0;
  const onlineCount = devices.filter((d) => d.status !== 'offline').length;
  const minWaterline = devices.length
    ? Math.round((devices.reduce((sum, d) => sum + d.minFreePercent, 0) / devices.length) * 10) / 10
    : 15;

  const cloudTotal = Math.max(0, toNum(cloudStorageState.totalGb, 0));
  const cloudUsed = clamp(toNum(cloudStorageState.usedGb, 0), 0, cloudTotal || Number.MAX_SAFE_INTEGER);
  const cloudFree = Math.max(0, cloudTotal - cloudUsed);
  const cloudUsedPercent = cloudTotal > 0 ? (cloudUsed / cloudTotal) * 100 : 0;

  return {
    total: Math.round(total * 100) / 100,
    used: Math.round(used * 100) / 100,
    free: Math.round(free * 100) / 100,
    usedPercent: Math.round(clamp(usedPercent, 0, 100) * 10) / 10,
    deviceCount: devices.length,
    onlineCount,
    cloud: {
      enabled: Boolean(cloudStorageState.enabled),
      provider: String(cloudStorageState.provider || 's3'),
      bucket: String(cloudStorageState.bucket || ''),
      region: String(cloudStorageState.region || ''),
      endpoint: String(cloudStorageState.endpoint || ''),
      syncStatus: String(cloudStorageState.syncStatus || 'disconnected'),
      lastSyncMs: Number(cloudStorageState.lastSyncMs || 0),
      totalGb: Math.round(cloudTotal * 100) / 100,
      usedGb: Math.round(cloudUsed * 100) / 100,
      freeGb: Math.round(cloudFree * 100) / 100,
      usedPercent: Math.round(clamp(cloudUsedPercent, 0, 100) * 10) / 10,
    },
    edge: {
      totalGb: Math.round(total * 100) / 100,
      usedGb: Math.round(used * 100) / 100,
      freeGb: Math.round(free * 100) / 100,
      usedPercent: Math.round(clamp(usedPercent, 0, 100) * 10) / 10,
      deviceCount: devices.length,
      onlineCount,
    },
    policy: {
      min_free_percent: minWaterline,
    },
    breakdown: {
      video: Math.round(used * 100) / 100,
      snapshots: 0,
      cache: 0,
    },
  };
}

function buildTrend(devices, days) {
  const now = Date.now();
  const dayMs = 24 * 60 * 60 * 1000;
  const points = [];
  for (let i = days - 1; i >= 0; i -= 1) {
    const bucketStart = now - (i + 1) * dayMs;
    const bucketEnd = now - i * dayMs;
    let weightedUsage = 0;
    let weightedCap = 0;
    for (const dev of devices) {
      const cap = Math.max(0, toNum(dev.totalGb, 0));
      if (cap <= 0) continue;
      const sample = (dev.telemetryHistory || []).find((item) => {
        const ts = toNum(item?.ts, 0);
        return ts >= bucketStart && ts < bucketEnd;
      });
      const histTotalGb = toNum(sample?.storageTotalGb, 0);
      const histUsedGb = toNum(sample?.storageUsedGb, 0);
      const usagePct = histTotalGb > 0
        ? clamp((histUsedGb / histTotalGb) * 100, 0, 100)
        : clamp(toNum(dev.usedPercent, 0), 0, 100);
      weightedUsage += (usagePct / 100) * cap;
      weightedCap += cap;
    }
    const usage = weightedCap > 0 ? (weightedUsage / weightedCap) * 100 : 0;
    const d = new Date(bucketEnd);
    points.push({
      date: d.toISOString().slice(0, 10),
      usage: Math.round(usage * 10) / 10,
    });
  }
  return points;
}

router.get('/overview', (req, res) => {
  const devices = collectStorageByDevice(req.user.id);
  res.json(aggregateOverview(devices));
});

router.get('/cloud-config', (req, res) => {
  const cloudTotal = Math.max(0, toNum(cloudStorageState.totalGb, 0));
  const cloudUsed = clamp(toNum(cloudStorageState.usedGb, 0), 0, cloudTotal || Number.MAX_SAFE_INTEGER);
  const cloudFree = Math.max(0, cloudTotal - cloudUsed);
  const cloudUsedPercent = cloudTotal > 0 ? (cloudUsed / cloudTotal) * 100 : 0;
  res.json({
    enabled: Boolean(cloudStorageState.enabled),
    provider: String(cloudStorageState.provider || 's3'),
    bucket: String(cloudStorageState.bucket || ''),
    region: String(cloudStorageState.region || ''),
    endpoint: String(cloudStorageState.endpoint || ''),
    syncStatus: String(cloudStorageState.syncStatus || 'disconnected'),
    lastSyncMs: Number(cloudStorageState.lastSyncMs || 0),
    totalGb: Math.round(cloudTotal * 100) / 100,
    usedGb: Math.round(cloudUsed * 100) / 100,
    freeGb: Math.round(cloudFree * 100) / 100,
    usedPercent: Math.round(clamp(cloudUsedPercent, 0, 100) * 10) / 10,
  });
});

router.post('/cloud-config', (req, res) => {
  const body = req.body || {};
  const enabled = body.enabled == null ? cloudStorageState.enabled : Boolean(body.enabled);
  const provider = String(body.provider || cloudStorageState.provider || 's3').toLowerCase();
  const bucket = String(body.bucket || '').trim();
  const region = String(body.region || '').trim();
  const endpoint = String(body.endpoint || '').trim();
  const totalGb = Math.max(1, toNum(body.total_gb ?? body.totalGb, cloudStorageState.totalGb));
  const usedGb = clamp(toNum(body.used_gb ?? body.usedGb, cloudStorageState.usedGb), 0, totalGb);

  cloudStorageState.enabled = enabled;
  cloudStorageState.provider = provider;
  cloudStorageState.bucket = bucket;
  cloudStorageState.region = region;
  cloudStorageState.endpoint = endpoint;
  cloudStorageState.totalGb = totalGb;
  cloudStorageState.usedGb = usedGb;
  cloudStorageState.syncStatus = enabled ? 'connected' : 'disconnected';
  cloudStorageState.lastSyncMs = enabled ? Date.now() : 0;

  const freeGb = Math.max(0, totalGb - usedGb);
  const usedPercent = totalGb > 0 ? (usedGb / totalGb) * 100 : 0;

  res.json({
    success: true,
    cloud: {
      enabled: cloudStorageState.enabled,
      provider: cloudStorageState.provider,
      bucket: cloudStorageState.bucket,
      region: cloudStorageState.region,
      endpoint: cloudStorageState.endpoint,
      syncStatus: cloudStorageState.syncStatus,
      lastSyncMs: cloudStorageState.lastSyncMs,
      totalGb: Math.round(totalGb * 100) / 100,
      usedGb: Math.round(usedGb * 100) / 100,
      freeGb: Math.round(freeGb * 100) / 100,
      usedPercent: Math.round(clamp(usedPercent, 0, 100) * 10) / 10,
    },
  });
});

router.get('/trend', (req, res) => {
  const days = clamp(Math.floor(toNum(req.query.days, 14)), 1, 30);
  const cameraId = Math.floor(toNum(req.query.camera_id, 0));
  const allDevices = collectStorageByDevice(req.user.id);
  const devices = cameraId > 0
    ? allDevices.filter((item) => Number(item.cameraId) === cameraId)
    : allDevices;
  res.json({
    trend: buildTrend(devices, days),
    scope: cameraId > 0 ? 'single' : 'all',
    cameraId: cameraId > 0 ? cameraId : null,
  });
});

router.get('/by-device', (req, res) => {
  const cameras = Camera.findByUserId(req.user.id);
  requestStorageRefresh(cameras);
  const devices = collectStorageByDevice(req.user.id, cameras).map((item) => ({
    id: item.id,
    cameraId: item.cameraId,
    streamKey: item.streamKey,
    name: item.name,
    status: item.status,
    totalGb: item.totalGb,
    usedGb: item.usedGb,
    freeGb: item.freeGb,
    usedPercent: item.usedPercent,
    minFreePercent: item.minFreePercent,
    lastUpdateMs: item.lastUpdateMs,
  }));
  res.json({ devices });
});

router.post('/policy', (req, res) => {
  if (!mqttControlService.isEnabled() || !mqttControlService.isReady()) {
    return res.status(503).json({ error: 'MQTT control channel not ready' });
  }

  const minFreePercent = clamp(Math.floor(toNum(req.body?.min_free_percent, 15)), 1, 95);
  const applyAll = Boolean(req.body?.apply_all !== false);
  const cameraId = toNum(req.body?.camera_id, 0);
  const cameras = Camera.findByUserId(req.user.id);
  const targets = applyAll
    ? cameras
    : cameras.filter((cam) => Number(cam.id) === Number(cameraId));

  if (!targets.length) {
    return res.status(404).json({ error: 'No target cameras found' });
  }

  const results = targets.map((cam) => ({
    cameraId: cam.id,
    streamKey: cam.stream_key,
    ok: mqttControlService.publishRecordPolicyCommand(
      cam.stream_key,
      minFreePercent
    ),
  }));

  const failed = results.filter((r) => !r.ok);
  if (failed.length) {
    return res.status(502).json({
      error: 'Failed to publish policy to some devices',
      results,
      policy: {
        min_free_percent: minFreePercent,
      },
    });
  }

  return res.json({
    success: true,
    results,
    policy: {
      min_free_percent: minFreePercent,
    },
  });
});

module.exports = router;
