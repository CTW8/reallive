const db = require('./db');

const DEFAULTS = Object.freeze({
  location: '',
  motion_enabled: 1,
  motion_sensitivity: 'High',
  person_enabled: 1,
  sound_enabled: 0,
  sound_sensitivity: 'Loud',
  detection_zones: '2 zones configured',
  night_vision_enabled: 1,
  night_vision_mode: 'Auto',
  image_flip_mode: 'Normal',
  watermark_enabled: 1,
  stream_profile: 'auto',
  stream_mode: 'auto',
  manual_level: 2,
  auto_min_level: 0,
  auto_max_level: 4,
  auto_policy: 'balanced',
  auto_cooldown_sec: 10,
  auto_up_hold_sec: 25,
  auto_down_hold_sec: 3,
  firmware_version: 'v2.3.8',
  firmware_update_available: 1,
});

function toBoolInt(value, fallback) {
  if (value == null) return fallback;
  if (typeof value === 'boolean') return value ? 1 : 0;
  const n = Number(value);
  if (Number.isFinite(n)) return n > 0 ? 1 : 0;
  return String(value).toLowerCase() === 'true' ? 1 : 0;
}

function cleanText(value, fallback) {
  if (value == null) return fallback;
  const s = String(value).trim();
  return s || fallback;
}

function clampInt(value, fallback, min, max) {
  if (value == null) return fallback;
  const n = Number(value);
  if (!Number.isFinite(n)) return fallback;
  return Math.max(min, Math.min(max, Math.round(n)));
}

function cleanMode(value, fallback) {
  if (value == null) return fallback;
  const s = String(value).trim().toLowerCase();
  if (s === 'manual' || s === 'auto') return s;
  return fallback;
}

function cleanPolicy(value, fallback) {
  if (value == null) return fallback;
  const s = String(value).trim().toLowerCase();
  if (s === 'stable' || s === 'balanced' || s === 'quality') return s;
  return fallback;
}

function cleanStreamProfile(value, fallback) {
  if (value == null) return fallback;
  const s = String(value).trim().toLowerCase();
  if (['auto', '360p', '540p', '720p', '1080p'].includes(s)) return s;
  return fallback;
}

function profileToLegacy(profile, current) {
  switch (profile) {
    case '360p':
      return { stream_mode: 'manual', manual_level: 0, auto_min_level: current.auto_min_level, auto_max_level: current.auto_max_level, auto_policy: current.auto_policy };
    case '540p':
      return { stream_mode: 'manual', manual_level: 1, auto_min_level: current.auto_min_level, auto_max_level: current.auto_max_level, auto_policy: current.auto_policy };
    case '720p':
      return { stream_mode: 'manual', manual_level: 2, auto_min_level: current.auto_min_level, auto_max_level: current.auto_max_level, auto_policy: current.auto_policy };
    case '1080p':
      return { stream_mode: 'manual', manual_level: 4, auto_min_level: current.auto_min_level, auto_max_level: current.auto_max_level, auto_policy: current.auto_policy };
    default:
      return { stream_mode: 'auto', manual_level: current.manual_level, auto_min_level: 0, auto_max_level: 4, auto_policy: 'balanced' };
  }
}

function legacyToProfile(streamMode, manualLevel) {
  if (String(streamMode || '').toLowerCase() !== 'manual') return 'auto';
  const lvl = Number(manualLevel) || 0;
  if (lvl <= 0) return '360p';
  if (lvl === 1) return '540p';
  if (lvl <= 3) return '720p';
  return '1080p';
}

function normalizeRow(row) {
  if (!row) return null;
  return {
    camera_id: Number(row.camera_id),
    location: String(row.location || ''),
    motion_enabled: Number(row.motion_enabled || 0) === 1,
    motion_sensitivity: String(row.motion_sensitivity || DEFAULTS.motion_sensitivity),
    person_enabled: Number(row.person_enabled || 0) === 1,
    sound_enabled: Number(row.sound_enabled || 0) === 1,
    sound_sensitivity: String(row.sound_sensitivity || DEFAULTS.sound_sensitivity),
    detection_zones: String(row.detection_zones || DEFAULTS.detection_zones),
    night_vision_enabled: Number(row.night_vision_enabled || 0) === 1,
    night_vision_mode: String(row.night_vision_mode || DEFAULTS.night_vision_mode),
    image_flip_mode: String(row.image_flip_mode || DEFAULTS.image_flip_mode),
    watermark_enabled: Number(row.watermark_enabled || 0) === 1,
    stream_profile: cleanStreamProfile(row.stream_profile, legacyToProfile(row.stream_mode, row.manual_level)),
    stream_mode: cleanMode(row.stream_mode, DEFAULTS.stream_mode),
    manual_level: clampInt(row.manual_level, DEFAULTS.manual_level, 0, 4),
    auto_min_level: clampInt(row.auto_min_level, DEFAULTS.auto_min_level, 0, 4),
    auto_max_level: clampInt(row.auto_max_level, DEFAULTS.auto_max_level, 0, 4),
    auto_policy: cleanPolicy(row.auto_policy, DEFAULTS.auto_policy),
    auto_cooldown_sec: clampInt(row.auto_cooldown_sec, DEFAULTS.auto_cooldown_sec, 3, 120),
    auto_up_hold_sec: clampInt(row.auto_up_hold_sec, DEFAULTS.auto_up_hold_sec, 5, 180),
    auto_down_hold_sec: clampInt(row.auto_down_hold_sec, DEFAULTS.auto_down_hold_sec, 1, 60),
    firmware_version: String(row.firmware_version || DEFAULTS.firmware_version),
    firmware_update_available: Number(row.firmware_update_available || 0) === 1,
    updated_at: row.updated_at || null,
  };
}

const CameraSettings = {
  getByCameraId(cameraId) {
    let row = db.prepare('SELECT * FROM camera_settings WHERE camera_id = ?').get(cameraId);
    if (!row) {
      db.prepare(`
        INSERT INTO camera_settings (
          camera_id, location, motion_enabled, motion_sensitivity, person_enabled, sound_enabled,
          sound_sensitivity, detection_zones, night_vision_enabled, night_vision_mode,
          image_flip_mode, watermark_enabled, stream_profile, stream_mode, manual_level, auto_min_level,
          auto_max_level, auto_policy, auto_cooldown_sec, auto_up_hold_sec, auto_down_hold_sec,
          firmware_version, firmware_update_available
        ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
      `).run(
        cameraId,
        DEFAULTS.location,
        DEFAULTS.motion_enabled,
        DEFAULTS.motion_sensitivity,
        DEFAULTS.person_enabled,
        DEFAULTS.sound_enabled,
        DEFAULTS.sound_sensitivity,
        DEFAULTS.detection_zones,
        DEFAULTS.night_vision_enabled,
        DEFAULTS.night_vision_mode,
        DEFAULTS.image_flip_mode,
        DEFAULTS.watermark_enabled,
        DEFAULTS.stream_profile,
        DEFAULTS.stream_mode,
        DEFAULTS.manual_level,
        DEFAULTS.auto_min_level,
        DEFAULTS.auto_max_level,
        DEFAULTS.auto_policy,
        DEFAULTS.auto_cooldown_sec,
        DEFAULTS.auto_up_hold_sec,
        DEFAULTS.auto_down_hold_sec,
        DEFAULTS.firmware_version,
        DEFAULTS.firmware_update_available
      );
      row = db.prepare('SELECT * FROM camera_settings WHERE camera_id = ?').get(cameraId);
    }
    return normalizeRow(row);
  },

  upsert(cameraId, patch = {}) {
    const current = this.getByCameraId(cameraId);
    const next = {
      location: cleanText(patch.location, current.location),
      motion_enabled: toBoolInt(patch.motion_enabled, current.motion_enabled ? 1 : 0),
      motion_sensitivity: cleanText(patch.motion_sensitivity, current.motion_sensitivity),
      person_enabled: toBoolInt(patch.person_enabled, current.person_enabled ? 1 : 0),
      sound_enabled: toBoolInt(patch.sound_enabled, current.sound_enabled ? 1 : 0),
      sound_sensitivity: cleanText(patch.sound_sensitivity, current.sound_sensitivity),
      detection_zones: cleanText(patch.detection_zones, current.detection_zones),
      night_vision_enabled: toBoolInt(patch.night_vision_enabled, current.night_vision_enabled ? 1 : 0),
      night_vision_mode: cleanText(patch.night_vision_mode, current.night_vision_mode),
      image_flip_mode: cleanText(patch.image_flip_mode, current.image_flip_mode),
      watermark_enabled: toBoolInt(patch.watermark_enabled, current.watermark_enabled ? 1 : 0),
      stream_profile: cleanStreamProfile(patch.stream_profile, current.stream_profile),
      stream_mode: cleanMode(patch.stream_mode, current.stream_mode),
      manual_level: clampInt(patch.manual_level, current.manual_level, 0, 4),
      auto_min_level: clampInt(patch.auto_min_level, current.auto_min_level, 0, 4),
      auto_max_level: clampInt(patch.auto_max_level, current.auto_max_level, 0, 4),
      auto_policy: cleanPolicy(patch.auto_policy, current.auto_policy),
      auto_cooldown_sec: clampInt(patch.auto_cooldown_sec, current.auto_cooldown_sec, 3, 120),
      auto_up_hold_sec: clampInt(patch.auto_up_hold_sec, current.auto_up_hold_sec, 5, 180),
      auto_down_hold_sec: clampInt(patch.auto_down_hold_sec, current.auto_down_hold_sec, 1, 60),
      firmware_version: cleanText(patch.firmware_version, current.firmware_version),
      firmware_update_available: toBoolInt(patch.firmware_update_available, current.firmware_update_available ? 1 : 0),
    };
    if (patch.stream_profile != null) {
      const mapped = profileToLegacy(next.stream_profile, next);
      next.stream_mode = mapped.stream_mode;
      next.manual_level = mapped.manual_level;
      next.auto_min_level = mapped.auto_min_level;
      next.auto_max_level = mapped.auto_max_level;
      next.auto_policy = mapped.auto_policy;
    } else {
      next.stream_profile = legacyToProfile(next.stream_mode, next.manual_level);
    }
    if (next.auto_min_level > next.auto_max_level) {
      const mid = next.auto_min_level;
      next.auto_min_level = next.auto_max_level;
      next.auto_max_level = mid;
    }
    db.prepare(`
      UPDATE camera_settings SET
        location = ?,
        motion_enabled = ?,
        motion_sensitivity = ?,
        person_enabled = ?,
        sound_enabled = ?,
        sound_sensitivity = ?,
        detection_zones = ?,
        night_vision_enabled = ?,
        night_vision_mode = ?,
        image_flip_mode = ?,
        watermark_enabled = ?,
        stream_profile = ?,
        stream_mode = ?,
        manual_level = ?,
        auto_min_level = ?,
        auto_max_level = ?,
        auto_policy = ?,
        auto_cooldown_sec = ?,
        auto_up_hold_sec = ?,
        auto_down_hold_sec = ?,
        firmware_version = ?,
        firmware_update_available = ?,
        updated_at = CURRENT_TIMESTAMP
      WHERE camera_id = ?
    `).run(
      next.location,
      next.motion_enabled,
      next.motion_sensitivity,
      next.person_enabled,
      next.sound_enabled,
      next.sound_sensitivity,
      next.detection_zones,
      next.night_vision_enabled,
      next.night_vision_mode,
      next.image_flip_mode,
      next.watermark_enabled,
      next.stream_profile,
      next.stream_mode,
      next.manual_level,
      next.auto_min_level,
      next.auto_max_level,
      next.auto_policy,
      next.auto_cooldown_sec,
      next.auto_up_hold_sec,
      next.auto_down_hold_sec,
      next.firmware_version,
      next.firmware_update_available,
      cameraId
    );
    return this.getByCameraId(cameraId);
  },
};

module.exports = CameraSettings;
