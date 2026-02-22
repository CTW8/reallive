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
          image_flip_mode, watermark_enabled, firmware_version, firmware_update_available
        ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
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
      firmware_version: cleanText(patch.firmware_version, current.firmware_version),
      firmware_update_available: toBoolInt(patch.firmware_update_available, current.firmware_update_available ? 1 : 0),
    };
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
      next.firmware_version,
      next.firmware_update_available,
      cameraId
    );
    return this.getByCameraId(cameraId);
  },
};

module.exports = CameraSettings;
