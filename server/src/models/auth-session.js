const db = require('./db');

const AuthSession = {
  create({
    userId,
    refreshTokenHash,
    expiresAtIso,
    deviceName = '',
    platform = '',
    appVersion = '',
    ipAddress = '',
    userAgent = '',
  }) {
    const stmt = db.prepare(`
      INSERT INTO auth_sessions (
        user_id, refresh_token_hash, device_name, platform, app_version,
        ip_address, user_agent, expires_at
      ) VALUES (?, ?, ?, ?, ?, ?, ?, ?)
    `);
    const result = stmt.run(
      userId,
      refreshTokenHash,
      deviceName,
      platform,
      appVersion,
      ipAddress,
      userAgent,
      expiresAtIso
    );
    return this.findById(result.lastInsertRowid);
  },

  findById(id) {
    return db.prepare('SELECT * FROM auth_sessions WHERE id = ?').get(id);
  },

  findByRefreshHash(refreshTokenHash) {
    return db.prepare('SELECT * FROM auth_sessions WHERE refresh_token_hash = ?').get(refreshTokenHash);
  },

  touch(sessionId, ipAddress = '', userAgent = '') {
    db.prepare(`
      UPDATE auth_sessions
      SET last_seen_at = CURRENT_TIMESTAMP,
          ip_address = COALESCE(NULLIF(?, ''), ip_address),
          user_agent = COALESCE(NULLIF(?, ''), user_agent)
      WHERE id = ?
    `).run(ipAddress, userAgent, sessionId);
  },

  rotateRefreshToken(sessionId, refreshTokenHash, expiresAtIso) {
    db.prepare(`
      UPDATE auth_sessions
      SET refresh_token_hash = ?, expires_at = ?, last_seen_at = CURRENT_TIMESTAMP
      WHERE id = ? AND revoked_at IS NULL
    `).run(refreshTokenHash, expiresAtIso, sessionId);
    return this.findById(sessionId);
  },

  revoke(sessionId) {
    const result = db.prepare(`
      UPDATE auth_sessions
      SET revoked_at = CURRENT_TIMESTAMP
      WHERE id = ? AND revoked_at IS NULL
    `).run(sessionId);
    return result.changes > 0;
  },

  revokeByUserId(userId) {
    db.prepare(`
      UPDATE auth_sessions
      SET revoked_at = CURRENT_TIMESTAMP
      WHERE user_id = ? AND revoked_at IS NULL
    `).run(userId);
  },

  isActive(sessionRow, nowMs = Date.now()) {
    if (!sessionRow) return false;
    if (sessionRow.revoked_at) return false;
    const exp = Date.parse(sessionRow.expires_at || '');
    if (!Number.isFinite(exp)) return false;
    return exp > nowMs;
  },

  cleanupExpired() {
    db.prepare('DELETE FROM auth_sessions WHERE expires_at <= CURRENT_TIMESTAMP OR revoked_at IS NOT NULL').run();
  },
};

module.exports = AuthSession;

