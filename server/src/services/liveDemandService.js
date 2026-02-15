const crypto = require('crypto');
const Camera = require('../models/camera');
const mqttControlService = require('./mqttControlService');
const edgeReplayService = require('./edgeReplayService');

const SESSION_TTL_MS = Math.max(5000, Number(process.env.WATCH_SESSION_TTL_MS || 25000));
const HEARTBEAT_INTERVAL_MS = Math.max(2000, Number(process.env.WATCH_HEARTBEAT_MS || 10000));
const LIVE_STOP_GRACE_MS = Math.max(0, Number(process.env.WATCH_LIVE_STOP_GRACE_MS || 30000));
const RECONCILE_INTERVAL_MS = Math.max(500, Number(process.env.WATCH_RECONCILE_MS || 1000));

const byCamera = new Map();
let timer = null;

function nowMs() {
  return Date.now();
}

function createCameraState(camera) {
  const now = nowMs();
  return {
    cameraId: Number(camera.id),
    streamKey: String(camera.stream_key || ''),
    sessions: new Map(),
    noViewerSince: now - LIVE_STOP_GRACE_MS - 1,
    desiredLive: true,
    lastAppliedLive: null,
    inFlight: false,
    lastError: '',
    lastUpdateMs: 0,
  };
}

function ensureCameraState(camera) {
  const key = Number(camera.id);
  let state = byCamera.get(key);
  if (!state) {
    state = createCameraState(camera);
    byCamera.set(key, state);
  } else {
    state.streamKey = String(camera.stream_key || state.streamKey || '');
  }
  return state;
}

function cleanupExpiredSessions(state, now) {
  for (const [sessionId, item] of state.sessions.entries()) {
    if (now - Number(item.lastSeen || 0) > SESSION_TTL_MS) {
      state.sessions.delete(sessionId);
    }
  }
}

async function applyLiveDesired(state) {
  if (state.inFlight) return;
  if (!state.streamKey) return;
  if (state.lastAppliedLive === state.desiredLive) return;

  state.inFlight = true;
  try {
    if (mqttControlService.isEnabled()) {
      if (!mqttControlService.isReady()) {
        state.lastError = 'mqtt not ready';
        return;
      }
      const ok = mqttControlService.publishLiveCommand(state.streamKey, state.desiredLive);
      if (!ok) {
        state.lastError = 'mqtt publish failed';
        return;
      }
      state.lastAppliedLive = state.desiredLive;
      state.lastError = '';
      state.lastUpdateMs = nowMs();
      return;
    }

    const runtime = await edgeReplayService.setLivePush(state.streamKey, state.desiredLive);
    if (!runtime) {
      state.lastError = 'control unavailable';
      return;
    }
    state.lastAppliedLive = Boolean(runtime.desiredLive);
    state.lastError = '';
    state.lastUpdateMs = nowMs();
  } catch (err) {
    state.lastError = err?.message || 'apply live failed';
  } finally {
    state.inFlight = false;
  }
}

async function reconcileCameraState(state, now) {
  cleanupExpiredSessions(state, now);
  const viewers = state.sessions.size;
  if (viewers > 0) {
    state.noViewerSince = 0;
    state.desiredLive = true;
  } else {
    if (!state.noViewerSince) state.noViewerSince = now;
    state.desiredLive = (now - state.noViewerSince) < LIVE_STOP_GRACE_MS;
  }
  await applyLiveDesired(state);
}

function ensureKnownCameras() {
  const all = Camera.findAll();
  for (const camera of all) {
    if (!camera?.stream_key) continue;
    ensureCameraState(camera);
  }
}

async function reconcileAll() {
  ensureKnownCameras();
  const now = nowMs();
  const states = Array.from(byCamera.values());
  for (const state of states) {
    await reconcileCameraState(state, now);
  }
}

function generateSessionId() {
  const rand = crypto.randomBytes(8).toString('hex');
  return `${nowMs()}_${rand}`;
}

function getPublicState(state) {
  const mqttState = mqttControlService.getDeviceState(state.streamKey);
  return {
    cameraId: state.cameraId,
    streamKey: state.streamKey,
    viewers: state.sessions.size,
    desiredLive: state.desiredLive,
    appliedLive: state.lastAppliedLive,
    inFlight: state.inFlight,
    lastError: state.lastError || null,
    noViewerSince: state.noViewerSince || null,
    device: mqttState,
    ttlMs: SESSION_TTL_MS,
    heartbeatMs: HEARTBEAT_INTERVAL_MS,
    stopGraceMs: LIVE_STOP_GRACE_MS,
  };
}

async function startWatchSession(camera, userId) {
  const state = ensureCameraState(camera);
  const id = generateSessionId();
  state.sessions.set(id, {
    sessionId: id,
    userId: Number(userId),
    lastSeen: nowMs(),
    createdAt: nowMs(),
  });
  state.noViewerSince = 0;
  state.desiredLive = true;
  await applyLiveDesired(state);
  return {
    sessionId: id,
    ...getPublicState(state),
  };
}

async function heartbeatWatchSession(camera, userId, sessionId) {
  const state = ensureCameraState(camera);
  const session = state.sessions.get(String(sessionId));
  if (!session || session.userId !== Number(userId)) {
    return { ok: false, found: false, ...getPublicState(state) };
  }
  session.lastSeen = nowMs();
  state.noViewerSince = 0;
  state.desiredLive = true;
  await applyLiveDesired(state);
  return { ok: true, found: true, ...getPublicState(state) };
}

async function stopWatchSession(camera, userId, sessionId) {
  const state = ensureCameraState(camera);
  const session = state.sessions.get(String(sessionId));
  if (session && session.userId === Number(userId)) {
    state.sessions.delete(String(sessionId));
  }
  await reconcileCameraState(state, nowMs());
  return { ok: true, ...getPublicState(state) };
}

function getCameraDemandState(cameraId) {
  const state = byCamera.get(Number(cameraId));
  if (!state) return null;
  cleanupExpiredSessions(state, nowMs());
  return getPublicState(state);
}

function start() {
  if (timer) return;
  timer = setInterval(() => {
    reconcileAll().catch((err) => {
      console.error('[LiveDemand] reconcile failed:', err?.message || err);
    });
  }, RECONCILE_INTERVAL_MS);
  console.log(`[LiveDemand] Started, heartbeat=${HEARTBEAT_INTERVAL_MS}ms ttl=${SESSION_TTL_MS}ms grace=${LIVE_STOP_GRACE_MS}ms`);
}

function stop() {
  if (timer) {
    clearInterval(timer);
    timer = null;
  }
}

module.exports = {
  start,
  stop,
  startWatchSession,
  heartbeatWatchSession,
  stopWatchSession,
  getCameraDemandState,
  HEARTBEAT_INTERVAL_MS,
  SESSION_TTL_MS,
};
