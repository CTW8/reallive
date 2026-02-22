const express = require('express');
const authMiddleware = require('../middleware/auth');
const Alert = require('../models/alert');
const Camera = require('../models/camera');
const CameraSettings = require('../models/camera-settings');
const UserSettings = require('../models/user-settings');
const { getSeiInfo } = require('../services/seiMonitor');
const { getTimeline } = require('../services/historyService');

const router = express.Router();
router.use(authMiddleware);
const lastHydrateByUser = new Map();
const HYDRATE_MIN_INTERVAL_MS = 5000;
const DEFAULT_NOTIFICATION_PREFS = Object.freeze({
  pushEnabled: true,
  motionEnabled: true,
  personEnabled: true,
  soundEnabled: false,
});

function getAlertPrefs(userId) {
  const stored = UserSettings.getByUserId(userId);
  const n = stored?.notifications || {};
  const toBool = (value, fallback) => {
    if (typeof value === 'boolean') return value;
    if (value == null) return fallback;
    return String(value).toLowerCase() === 'true';
  };
  return {
    pushEnabled: toBool(n.email, DEFAULT_NOTIFICATION_PREFS.pushEnabled),
    motionEnabled: toBool(n.sms, DEFAULT_NOTIFICATION_PREFS.motionEnabled),
    personEnabled: toBool(n.webhook, DEFAULT_NOTIFICATION_PREFS.personEnabled),
    soundEnabled: toBool(n.sound, DEFAULT_NOTIFICATION_PREFS.soundEnabled),
  };
}

function canSeeAlertByPrefs(alert, prefs) {
  const t = String(alert?.type || '').toLowerCase();
  if (t.includes('person')) return prefs.personEnabled;
  if (t.includes('motion')) return prefs.motionEnabled;
  if (t.includes('sound') || t.includes('audio') || t.includes('noise')) return prefs.soundEnabled;
  return true;
}

function detectionKind(rawType) {
  const t = String(rawType || '').toLowerCase();
  if (t.includes('person')) return 'person';
  if (t.includes('motion')) return 'motion';
  if (t.includes('sound') || t.includes('audio') || t.includes('noise')) return 'sound';
  return 'other';
}

function filterAlertsByPrefs(items, prefs) {
  if (!Array.isArray(items) || !items.length) return [];
  return items.filter((row) => canSeeAlertByPrefs(row, prefs));
}

function getCameraSettingsMap(userId) {
  const map = new Map();
  const cameras = Camera.findByUserId(userId);
  for (const cam of cameras) {
    try {
      map.set(Number(cam.id), CameraSettings.getByCameraId(cam.id));
    } catch {
      map.set(Number(cam.id), null);
    }
  }
  return map;
}

function canSeeAlertByCameraSettings(alert, cameraSettingsMap) {
  const cameraId = Number(alert?.camera_id || 0);
  if (!Number.isFinite(cameraId) || cameraId <= 0) return true;
  const cs = cameraSettingsMap.get(cameraId);
  if (!cs) return true;
  const kind = detectionKind(alert?.type);
  if (kind === 'person') return Boolean(cs.person_enabled);
  if (kind === 'motion') return Boolean(cs.motion_enabled);
  if (kind === 'sound') return Boolean(cs.sound_enabled);
  return true;
}

function filterAlertsBySettings(items, prefs, cameraSettingsMap) {
  if (!Array.isArray(items) || !items.length) return [];
  return items.filter((row) => {
    return canSeeAlertByPrefs(row, prefs) && canSeeAlertByCameraSettings(row, cameraSettingsMap);
  });
}

function parseAlertEventTsMs(alert) {
  const desc = String(alert?.description || '');
  const marker = desc.match(/\[evt:(\d{10,16}):/);
  if (marker && marker[1]) {
    const raw = Number(marker[1]);
    if (Number.isFinite(raw) && raw > 0) {
      if (raw > 1e15) return Math.floor(raw / 1000);
      if (raw < 1e12) return Math.floor(raw * 1000);
      return Math.floor(raw);
    }
  }
  const createdMs = new Date(alert?.created_at || 0).getTime();
  if (Number.isFinite(createdMs) && createdMs > 0) return Math.floor(createdMs);
  return Date.now();
}

function enrichAlertsWithEventTs(userId, items) {
  if (!Array.isArray(items) || !items.length) return items || [];
  const cameras = Camera.findByUserId(userId);
  const cameraById = new Map(cameras.map((cam) => [Number(cam.id), cam]));
  const timelineCache = new Map();

  return items.map((row) => {
    const out = { ...row };
    let tsMs = parseAlertEventTsMs(out);

    if (String(out.type || '').toLowerCase().includes('person') && Number(out.camera_id) > 0) {
      const hasMarker = /\[evt:\d{10,16}:/.test(String(out.description || ''));
      if (!hasMarker) {
        const camera = cameraById.get(Number(out.camera_id));
        if (camera) {
          const cacheKey = `${camera.stream_key}:${Math.floor(tsMs / 60000)}`;
          let nearbyEvents = timelineCache.get(cacheKey);
          if (!nearbyEvents) {
            const start = tsMs - 5 * 60 * 1000;
            const end = tsMs + 5 * 60 * 1000;
            const timeline = getTimeline(camera.stream_key, { start, end });
            nearbyEvents = (timeline?.events || [])
              .filter((evt) => String(evt?.type || '').toLowerCase() === 'person-detected')
              .map((evt) => Number(evt.ts))
              .filter((v) => Number.isFinite(v));
            timelineCache.set(cacheKey, nearbyEvents);
          }
          if (nearbyEvents.length) {
            let best = nearbyEvents[0];
            let bestDiff = Math.abs(best - tsMs);
            for (let i = 1; i < nearbyEvents.length; i++) {
              const diff = Math.abs(nearbyEvents[i] - tsMs);
              if (diff < bestDiff) {
                best = nearbyEvents[i];
                bestDiff = diff;
              }
            }
            if (bestDiff <= 10 * 60 * 1000) {
              tsMs = best;
            }
          }
        }
      }
    }

    out.event_ts_ms = Math.floor(tsMs);
    return out;
  });
}

function canGenerateByCameraSettings(kind, cameraSettings) {
  if (!cameraSettings) return true;
  if (kind === 'person') return Boolean(cameraSettings.person_enabled);
  if (kind === 'motion') return Boolean(cameraSettings.motion_enabled);
  if (kind === 'sound') return Boolean(cameraSettings.sound_enabled);
  return true;
}

function hydrateDetectionAlertsForUser(userId) {
  const now = Date.now();
  const last = Number(lastHydrateByUser.get(userId) || 0);
  if (now - last < HYDRATE_MIN_INTERVAL_MS) return;
  lastHydrateByUser.set(userId, now);

  const cameras = Camera.findByUserId(userId);
  for (const camera of cameras) {
    const cameraSettings = CameraSettings.getByCameraId(camera.id);
    try {
      const seen = new Set();
      const sei = getSeiInfo(camera.stream_key);
      const seiEvents = Array.isArray(sei?.personEvents) ? sei.personEvents : [];
      for (const evt of seiEvents.slice(-50)) {
        const kind = detectionKind(evt?.type || 'person-detected');
        if (!canGenerateByCameraSettings(kind, cameraSettings)) continue;
        const key = `${kind}:${Math.floor(Number(evt?.ts || 0))}:${evt?.bbox?.x || 0}:${evt?.bbox?.y || 0}:${evt?.bbox?.w || 0}:${evt?.bbox?.h || 0}`;
        if (seen.has(key)) continue;
        seen.add(key);
        Alert.createDetectionEvent(userId, camera, evt?.type || 'person-detected', evt);
      }

      const timelineStart = now - 24 * 3600 * 1000;
      const timeline = getTimeline(camera.stream_key, { start: timelineStart, end: now });
      const timelineEvents = Array.isArray(timeline?.events) ? timeline.events : [];
      for (const evt of timelineEvents.slice(-400)) {
        const kind = detectionKind(evt?.type);
        if (kind === 'other') continue;
        if (!canGenerateByCameraSettings(kind, cameraSettings)) continue;
        const key = `${kind}:${Math.floor(Number(evt?.ts || 0))}:${evt?.bbox?.x || 0}:${evt?.bbox?.y || 0}:${evt?.bbox?.w || 0}:${evt?.bbox?.h || 0}`;
        if (seen.has(key)) continue;
        seen.add(key);
        Alert.createDetectionEvent(userId, camera, evt?.type, evt);
      }
    } catch (err) {
      console.error('[Alerts] hydrate detection events failed:', err?.message || err);
    }
  }
}

router.get('/', (req, res) => {
  hydrateDetectionAlertsForUser(req.user.id);
  const prefs = getAlertPrefs(req.user.id);
  const cameraSettingsMap = getCameraSettingsMap(req.user.id);
  const limitRaw = req.query.limit ? parseInt(req.query.limit, 10) : undefined;
  const offsetRaw = req.query.offset ? parseInt(req.query.offset, 10) : undefined;
  const pageRaw = req.query.page ? parseInt(req.query.page, 10) : undefined;
  const limit = Number.isFinite(limitRaw) ? limitRaw : undefined;
  const offset = Number.isFinite(offsetRaw) ? offsetRaw : undefined;
  const page = Number.isFinite(pageRaw) ? pageRaw : undefined;
  const paged = String(req.query.paged || '') === '1' || page != null || offset != null;

  const filters = {
    type: req.query.type,
    typeGroup: req.query.type_group,
    status: req.query.status,
    q: req.query.q,
    since: req.query.since,
    until: req.query.until,
    limit: limit,
    offset: offset != null ? offset : (page != null && limit != null ? Math.max(0, (page - 1) * limit) : undefined),
  };

  if (paged) {
    const safeLimit = Math.max(1, Math.min(200, Number(filters.limit) || 20));
    const safeOffset = Math.max(0, Number(filters.offset) || 0);
    const rawAll = Alert.findByUserId(req.user.id, { ...filters, limit: 5000 });
    const filtered = filterAlertsBySettings(
      enrichAlertsWithEventTs(req.user.id, rawAll),
      prefs,
      cameraSettingsMap
    );
    const total = filtered.length;
    const items = filtered.slice(safeOffset, safeOffset + safeLimit);
    return res.json({
      items,
      total,
      limit: safeLimit,
      offset: safeOffset,
      page: Math.floor(safeOffset / safeLimit) + 1,
      total_pages: Math.max(1, Math.ceil(total / safeLimit)),
    });
  }

  const alerts = Alert.findByUserId(req.user.id, filters);
  return res.json(filterAlertsBySettings(
    enrichAlertsWithEventTs(req.user.id, alerts),
    prefs,
    cameraSettingsMap
  ));
});

router.get('/stats', (req, res) => {
  const stats = Alert.getStats(req.user.id);
  res.json(stats);
});

router.get('/unread-count', (req, res) => {
  const prefs = getAlertPrefs(req.user.id);
  const cameraSettingsMap = getCameraSettingsMap(req.user.id);
  if (!prefs.pushEnabled) {
    return res.json({ count: 0 });
  }
  const unread = Alert.findByUserId(req.user.id, { status: 'new', limit: 5000 });
  const count = filterAlertsBySettings(unread, prefs, cameraSettingsMap).length;
  res.json({ count });
});

router.get('/:id', (req, res) => {
  const prefs = getAlertPrefs(req.user.id);
  const cameraSettingsMap = getCameraSettingsMap(req.user.id);
  const alert = Alert.findById(req.params.id, req.user.id);
  if (!alert) {
    return res.status(404).json({ error: 'Alert not found' });
  }
  if (!canSeeAlertByPrefs(alert, prefs) || !canSeeAlertByCameraSettings(alert, cameraSettingsMap)) {
    return res.status(404).json({ error: 'Alert not found' });
  }
  res.json(alert);
});

router.post('/', (req, res) => {
  const { camera_id, type, title, description, status } = req.body;
  if (!type || !title) {
    return res.status(400).json({ error: 'Type and title are required' });
  }
  const alert = Alert.create(req.user.id, { camera_id, type, title, description, status });
  res.status(201).json(alert);
});

router.patch('/:id', (req, res) => {
  const alert = Alert.findById(req.params.id, req.user.id);
  if (!alert) {
    return res.status(404).json({ error: 'Alert not found' });
  }
  const updated = Alert.update(req.params.id, req.user.id, req.body);
  res.json(updated);
});

router.post('/:id/read', (req, res) => {
  const alert = Alert.findById(req.params.id, req.user.id);
  if (!alert) {
    return res.status(404).json({ error: 'Alert not found' });
  }
  const updated = Alert.markRead(req.params.id, req.user.id);
  res.json(updated);
});

router.post('/:id/resolve', (req, res) => {
  const alert = Alert.findById(req.params.id, req.user.id);
  if (!alert) {
    return res.status(404).json({ error: 'Alert not found' });
  }
  const updated = Alert.resolve(req.params.id, req.user.id);
  res.json(updated);
});

router.post('/batch', (req, res) => {
  const { ids, action } = req.body;
  if (!ids || !Array.isArray(ids) || ids.length === 0) {
    return res.status(400).json({ error: 'ids array is required' });
  }

  switch (action) {
    case 'read':
      Alert.batchUpdate(ids, req.user.id, 'read');
      break;
    case 'resolve':
      Alert.batchUpdate(ids, req.user.id, 'resolved');
      break;
    case 'delete':
      Alert.batchDelete(ids, req.user.id);
      break;
    default:
      return res.status(400).json({ error: 'Invalid action. Use read, resolve, or delete' });
  }

  res.json({ success: true, affected: ids.length });
});

router.delete('/:id', (req, res) => {
  const alert = Alert.findById(req.params.id, req.user.id);
  if (!alert) {
    return res.status(404).json({ error: 'Alert not found' });
  }
  Alert.delete(req.params.id, req.user.id);
  res.json({ message: 'Alert deleted' });
});

module.exports = router;
