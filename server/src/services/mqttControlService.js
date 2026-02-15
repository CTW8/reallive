const config = require('../config');

let mqttLib = null;
try {
  mqttLib = require('mqtt');
} catch {
  mqttLib = null;
}

const mqttCfg = config.mqttControl || {};
const enabled = Boolean(mqttCfg.enabled);
const brokerUrl = String(mqttCfg.brokerUrl || 'mqtt://127.0.0.1:1883');
const username = String(mqttCfg.username || '');
const password = String(mqttCfg.password || '');
const clientId = String(mqttCfg.clientId || `reallive-server-${process.pid}`);
const topicPrefixRaw = String(mqttCfg.topicPrefix || 'reallive/device');
const topicPrefix = topicPrefixRaw.replace(/\/+$/, '');
const commandQos = Math.max(0, Math.min(2, Number(mqttCfg.commandQos ?? 1)));
const stateQos = Math.max(0, Math.min(2, Number(mqttCfg.stateQos ?? 0)));
const commandRetain = mqttCfg.commandRetain !== false;
const stateStaleMs = Math.max(3000, Number(mqttCfg.stateStaleMs ?? 12000));

let client = null;
let ready = false;
const stateByStream = new Map();
const lastStatusByStream = new Map();
let staleTimer = null;
let stateEventEmitter = null;
let seq = Date.now();

function nowMs() {
  return Date.now();
}

function sanitizeToken(raw) {
  const s = String(raw || '');
  return s.replace(/[^a-zA-Z0-9._-]/g, '_');
}

function commandTopic(streamKey) {
  return `${topicPrefix}/${sanitizeToken(streamKey)}/command`;
}

function stateTopicWildcard() {
  return `${topicPrefix}/+/state`;
}

function parseJsonSafe(payload) {
  if (!payload) return null;
  try {
    const obj = JSON.parse(payload);
    if (obj && typeof obj === 'object') return obj;
  } catch {
  }
  return null;
}

function toBool(value, fallback = false) {
  if (typeof value === 'boolean') return value;
  if (typeof value === 'number') return value !== 0;
  if (typeof value === 'string') {
    const s = value.toLowerCase();
    if (s === 'true' || s === '1' || s === 'yes' || s === 'on') return true;
    if (s === 'false' || s === '0' || s === 'no' || s === 'off') return false;
  }
  return fallback;
}

function normalizeRuntimeState(raw) {
  if (!raw || typeof raw !== 'object') return null;
  return {
    ts: Number(raw.ts || nowMs()),
    running: toBool(raw.running, false),
    desiredLive: toBool(raw.desiredLive ?? raw.desired_live, false),
    activeLive: toBool(raw.activeLive ?? raw.active_live, false),
    reason: raw.reason ? String(raw.reason) : null,
    commandSeq: Number(raw.commandSeq ?? raw.command_seq ?? -1),
    updatedAt: nowMs(),
  };
}

function deriveStatus(runtime) {
  if (!runtime) return 'offline';
  if (toBool(runtime.activeLive, false)) return 'streaming';
  return 'online';
}

function emitStateEvent(streamKey, runtime, forceStatus = null) {
  const status = forceStatus || deriveStatus(runtime);
  const last = lastStatusByStream.get(streamKey);
  const changed = last !== status;
  lastStatusByStream.set(streamKey, status);
  if (typeof stateEventEmitter === 'function') {
    stateEventEmitter(streamKey, {
      status,
      changed,
      runtime: runtime ? { ...runtime } : null,
      ts: nowMs(),
    });
  }
}

function sweepStaleStates() {
  const now = nowMs();
  const removed = [];
  for (const [streamKey, runtime] of stateByStream.entries()) {
    if (now - Number(runtime.updatedAt || 0) > stateStaleMs) {
      stateByStream.delete(streamKey);
      removed.push(streamKey);
    }
  }
  for (const streamKey of removed) {
    emitStateEvent(streamKey, null, 'offline');
  }
}

function start() {
  if (!enabled) {
    console.log('[MQTT Control] Disabled');
    return;
  }
  if (!mqttLib) {
    console.error('[MQTT Control] mqtt package not installed');
    return;
  }
  if (client) return;

  client = mqttLib.connect(brokerUrl, {
    username: username || undefined,
    password: password || undefined,
    clientId,
    reconnectPeriod: 1000,
    keepalive: 30,
    clean: true,
  });

  client.on('connect', () => {
    ready = true;
    client.subscribe(stateTopicWildcard(), { qos: stateQos }, (err) => {
      if (err) {
        console.error('[MQTT Control] subscribe state failed:', err.message);
      } else {
        console.log(`[MQTT Control] Connected ${brokerUrl}, subscribed ${stateTopicWildcard()}`);
      }
    });
  });

  client.on('reconnect', () => {
    ready = false;
  });

  client.on('close', () => {
    ready = false;
  });

  client.on('error', (err) => {
    console.error('[MQTT Control] client error:', err.message);
  });

  client.on('message', (topic, payloadBuf) => {
    const payload = payloadBuf ? payloadBuf.toString('utf8') : '';
    const parsed = parseJsonSafe(payload);
    const runtime = normalizeRuntimeState(parsed);
    if (!runtime) return;
    const payloadKey = sanitizeToken(parsed?.stream_key || parsed?.streamKey || '');
    const parts = String(topic || '').split('/');
    const topicKey = parts.length >= 2 ? parts[parts.length - 2] : '';
    const streamKey = payloadKey || topicKey;
    if (!streamKey) return;
    stateByStream.set(streamKey, runtime);
    emitStateEvent(streamKey, runtime);
  });

  staleTimer = setInterval(sweepStaleStates, Math.max(1000, Math.floor(stateStaleMs / 2)));
}

function stop() {
  if (!client) return;
  try {
    client.end(true);
  } catch {
  }
  ready = false;
  client = null;
  if (staleTimer) {
    clearInterval(staleTimer);
    staleTimer = null;
  }
  stateByStream.clear();
  lastStatusByStream.clear();
}

function isEnabled() {
  return enabled;
}

function isReady() {
  return enabled && ready && !!client;
}

function publishLiveCommand(streamKey, enable) {
  if (!isReady()) return false;
  const token = sanitizeToken(streamKey);
  if (!token) return false;
  seq += 1;
  const payload = {
    v: 1,
    ts: nowMs(),
    source: 'server',
    stream_key: streamKey,
    type: 'live',
    enable: !!enable,
    seq,
  };
  const topic = commandTopic(streamKey);
  client.publish(topic, JSON.stringify(payload), {
    qos: commandQos,
    retain: commandRetain,
  });
  return true;
}

function getDeviceState(streamKey) {
  const token = sanitizeToken(streamKey);
  if (!token) return null;
  const state = stateByStream.get(token);
  if (state && nowMs() - Number(state.updatedAt || 0) > stateStaleMs) {
    stateByStream.delete(token);
    emitStateEvent(token, null, 'offline');
    return null;
  }
  return state ? { ...state } : null;
}

function setStateEventEmitter(fn) {
  stateEventEmitter = typeof fn === 'function' ? fn : null;
}

module.exports = {
  start,
  stop,
  isEnabled,
  isReady,
  publishLiveCommand,
  getDeviceState,
  setStateEventEmitter,
};
