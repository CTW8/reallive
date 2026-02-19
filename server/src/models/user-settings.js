const db = require('./db');

function parseJsonSafe(raw) {
  try {
    const obj = JSON.parse(String(raw || '{}'));
    return obj && typeof obj === 'object' ? obj : {};
  } catch {
    return {};
  }
}

function stringifyJsonSafe(obj) {
  return JSON.stringify(obj && typeof obj === 'object' ? obj : {});
}

const UserSettings = {
  getByUserId(userId) {
    const row = db.prepare(
      'SELECT * FROM user_settings WHERE user_id = ?'
    ).get(userId);
    if (!row) return null;
    return {
      userId: Number(row.user_id),
      profile: parseJsonSafe(row.profile_json),
      notifications: parseJsonSafe(row.notification_json),
      system: parseJsonSafe(row.system_json),
      security: parseJsonSafe(row.security_json),
      updatedAt: row.updated_at,
    };
  },

  upsert(userId, sections = {}) {
    const current = this.getByUserId(userId) || {
      profile: {},
      notifications: {},
      system: {},
      security: {},
    };
    const merged = {
      profile: { ...current.profile, ...(sections.profile || {}) },
      notifications: { ...current.notifications, ...(sections.notifications || {}) },
      system: { ...current.system, ...(sections.system || {}) },
      security: { ...current.security, ...(sections.security || {}) },
    };

    db.prepare(`
      INSERT INTO user_settings (user_id, profile_json, notification_json, system_json, security_json, updated_at)
      VALUES (?, ?, ?, ?, ?, CURRENT_TIMESTAMP)
      ON CONFLICT(user_id) DO UPDATE SET
        profile_json = excluded.profile_json,
        notification_json = excluded.notification_json,
        system_json = excluded.system_json,
        security_json = excluded.security_json,
        updated_at = CURRENT_TIMESTAMP
    `).run(
      userId,
      stringifyJsonSafe(merged.profile),
      stringifyJsonSafe(merged.notifications),
      stringifyJsonSafe(merged.system),
      stringifyJsonSafe(merged.security)
    );

    return this.getByUserId(userId);
  },
};

module.exports = UserSettings;
