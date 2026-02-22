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

function toText(value, fallback = '') {
  if (value == null) return fallback;
  const text = String(value).trim();
  return text || fallback;
}

function normalizeProfile(value, fallback = 'auto') {
  const p = toText(value, fallback).toLowerCase();
  if (['auto', '360p', '540p', '720p', '1080p'].includes(p)) return p;
  return fallback;
}

function normalizeRuntimeState(raw) {
  if (!raw || typeof raw !== 'object') return null;
  const storageTotalGb = Math.max(0, Number(raw.storageTotalGb ?? raw.storage_total_gb ?? 0) || 0);
  const storageUsedGbRaw = Math.max(0, Number(raw.storageUsedGb ?? raw.storage_used_gb ?? 0) || 0);
  const storageUsedGb = Math.min(storageTotalGb || Number.MAX_SAFE_INTEGER, storageUsedGbRaw);
  const storagePct = storageTotalGb > 0
    ? Math.max(0, Math.min(100, (storageUsedGb / storageTotalGb) * 100))
    : Math.max(0, Math.min(100, Number(raw.storagePct ?? raw.storage_pct ?? 0) || 0));
  return {
    ts: Number(raw.ts || nowMs()),
    running: toBool(raw.running, false),
    desiredLive: toBool(raw.desiredLive ?? raw.desired_live, false),
    activeLive: toBool(raw.activeLive ?? raw.active_live, false),
    recordMinFreePercent: Math.max(1, Math.min(95, Number(raw.recordMinFreePercent ?? raw.record_min_free_percent ?? 15) || 15)),
    motionEnabled: toBool(raw.motionEnabled ?? raw.motion_enabled, true),
    personEnabled: toBool(raw.personEnabled ?? raw.person_enabled, true),
    soundEnabled: toBool(raw.soundEnabled ?? raw.sound_enabled, false),
    motionSensitivity: toText(raw.motionSensitivity ?? raw.motion_sensitivity, 'High'),
    soundSensitivity: toText(raw.soundSensitivity ?? raw.sound_sensitivity, 'Loud'),
    detectionZones: toText(raw.detectionZones ?? raw.detection_zones, '2 zones configured'),
    watermarkEnabled: toBool(raw.watermarkEnabled ?? raw.watermark_enabled, true),
    imageFlipMode: Number(raw.imageFlipMode ?? raw.image_flip_mode ?? 0) || 0,
    nightVisionEnabled: toBool(raw.nightVisionEnabled ?? raw.night_vision_enabled, false),
    nightVisionMode: Number(raw.nightVisionMode ?? raw.night_vision_mode ?? 0) || 0,
    streamMode: toText(raw.streamMode ?? raw.stream_mode, 'auto'),
    streamProfile: normalizeProfile(raw.streamProfile ?? raw.stream_profile, 'auto'),
    profileLevel: Math.max(0, Math.min(4, Number(raw.profileLevel ?? raw.profile_level ?? 2) || 2)),
    targetFps: Math.max(1, Number(raw.targetFps ?? raw.target_fps ?? 15) || 15),
    targetBitrateKbps: Math.max(100, Number(raw.targetBitrateKbps ?? raw.target_bitrate_kbps ?? 600) || 600),
    autoPolicy: toText(raw.autoPolicy ?? raw.auto_policy, 'balanced'),
    autoMinLevel: Math.max(0, Math.min(4, Number(raw.autoMinLevel ?? raw.auto_min_level ?? 0) || 0)),
    autoMaxLevel: Math.max(0, Math.min(4, Number(raw.autoMaxLevel ?? raw.auto_max_level ?? 4) || 4)),
    adaptationReason: toText(raw.adaptationReason ?? raw.adaptation_reason, ''),
    storagePct: Math.round(storagePct * 10) / 10,
    storageUsedGb: Math.round(storageUsedGb * 100) / 100,
    storageTotalGb: Math.round(storageTotalGb * 100) / 100,
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

function publishRecordPolicyCommand(streamKey, minFreePercent) {
  if (!isReady()) return false;
  const token = sanitizeToken(streamKey);
  if (!token) return false;
  const min = Math.max(1, Math.min(95, Number(minFreePercent) || 15));
  seq += 1;
  const payload = {
    v: 1,
    ts: nowMs(),
    source: 'server',
    stream_key: streamKey,
    type: 'record_policy',
    min_free_percent: min,
    seq,
  };
  const topic = commandTopic(streamKey);
  client.publish(topic, JSON.stringify(payload), {
    qos: commandQos,
    retain: commandRetain,
  });
  return true;
}

function publishStorageQueryCommand(streamKey) {
  if (!isReady()) return false;
  const token = sanitizeToken(streamKey);
  if (!token) return false;
  seq += 1;
  const payload = {
    v: 1,
    ts: nowMs(),
    source: 'server',
    stream_key: streamKey,
    type: 'storage_query',
    seq,
  };
  const topic = commandTopic(streamKey);
  client.publish(topic, JSON.stringify(payload), {
    qos: commandQos,
    retain: false,
  });
  return true;
}

function publishCameraSettingsCommand(streamKey, settings = {}) {
  if (!isReady()) return false;
  const token = sanitizeToken(streamKey);
  if (!token) return false;
  seq += 1;
  const motionEnabled = toBool(settings.motion_enabled, true);
  const personEnabled = toBool(settings.person_enabled, true);
  const soundEnabled = toBool(settings.sound_enabled, false);
  const nightVisionEnabled = toBool(settings.night_vision_enabled, true);
  const watermarkEnabled = toBool(settings.watermark_enabled, true);
  const streamMode = toText(settings.stream_mode, 'auto').toLowerCase() === 'manual' ? 'manual' : 'auto';
  const streamProfile = normalizeProfile(settings.stream_profile, streamMode === 'manual' ? '720p' : 'auto');
  const manualLevel = Math.max(0, Math.min(4, Number(settings.manual_level ?? 2) || 2));
  const autoMinLevel = Math.max(0, Math.min(4, Number(settings.auto_min_level ?? 0) || 0));
  const autoMaxLevel = Math.max(0, Math.min(4, Number(settings.auto_max_level ?? 4) || 4));
  const autoPolicyRaw = toText(settings.auto_policy, 'balanced').toLowerCase();
  const autoPolicy = ['stable', 'balanced', 'quality'].includes(autoPolicyRaw) ? autoPolicyRaw : 'balanced';
  const autoCooldownSec = Math.max(3, Math.min(120, Number(settings.auto_cooldown_sec ?? 10) || 10));
  const autoUpHoldSec = Math.max(5, Math.min(180, Number(settings.auto_up_hold_sec ?? 25) || 25));
  const autoDownHoldSec = Math.max(1, Math.min(60, Number(settings.auto_down_hold_sec ?? 3) || 3));
  const motionSensitivity = settings.motion_sensitivity != null ? String(settings.motion_sensitivity) : undefined;
  const soundSensitivity = settings.sound_sensitivity != null ? String(settings.sound_sensitivity) : undefined;
  const nightVisionMode = settings.night_vision_mode != null ? String(settings.night_vision_mode) : undefined;
  const imageFlipMode = settings.image_flip_mode != null ? String(settings.image_flip_mode) : undefined;
  const detectionZones = settings.detection_zones != null ? String(settings.detection_zones) : undefined;

  const payload = {
    v: 1,
    ts: nowMs(),
    source: 'server',
    stream_key: streamKey,
    type: 'camera_settings',
    seq,
    motion_enabled: motionEnabled,
    person_enabled: personEnabled,
    sound_enabled: soundEnabled,
    stream_mode: streamMode,
    stream_profile: streamProfile,
    manual_level: manualLevel,
    auto_min_level: autoMinLevel,
    auto_max_level: autoMaxLevel,
    auto_policy: autoPolicy,
    auto_cooldown_sec: autoCooldownSec,
    auto_up_hold_sec: autoUpHoldSec,
    auto_down_hold_sec: autoDownHoldSec,
    night_vision_enabled: nightVisionEnabled,
    watermark_enabled: watermarkEnabled,
    motion_sensitivity: motionSensitivity,
    sound_sensitivity: soundSensitivity,
    night_vision_mode: nightVisionMode,
    image_flip_mode: imageFlipMode,
    detection_zones: detectionZones,
    settings: {
      motion_enabled: motionEnabled,
      person_enabled: personEnabled,
      sound_enabled: soundEnabled,
      stream_mode: streamMode,
      stream_profile: streamProfile,
      manual_level: manualLevel,
      auto_min_level: autoMinLevel,
      auto_max_level: autoMaxLevel,
      auto_policy: autoPolicy,
      auto_cooldown_sec: autoCooldownSec,
      auto_up_hold_sec: autoUpHoldSec,
      auto_down_hold_sec: autoDownHoldSec,
      night_vision_enabled: nightVisionEnabled,
      watermark_enabled: watermarkEnabled,
      motion_sensitivity: motionSensitivity,
      sound_sensitivity: soundSensitivity,
      night_vision_mode: nightVisionMode,
      image_flip_mode: imageFlipMode,
      detection_zones: detectionZones,
    },
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
  publishRecordPolicyCommand,
  publishStorageQueryCommand,
  publishCameraSettingsCommand,
  getDeviceState,
  setStateEventEmitter,
};
