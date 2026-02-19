const db = require('./db');

const Session = {
  create(cameraId) {
    const stmt = db.prepare(
      'INSERT INTO sessions (camera_id) VALUES (?)'
    );
    const result = stmt.run(cameraId);
    return db.prepare('SELECT * FROM sessions WHERE id = ?').get(result.lastInsertRowid);
  },

  endSession(sessionId) {
    db.prepare(
      "UPDATE sessions SET end_time = CURRENT_TIMESTAMP, status = 'completed' WHERE id = ?"
    ).run(sessionId);
  },

  endSessionByUser(sessionId, userId) {
    const row = db.prepare(`
      SELECT s.id
      FROM sessions s
      JOIN cameras c ON c.id = s.camera_id
      WHERE s.id = ? AND c.user_id = ?
      LIMIT 1
    `).get(sessionId, userId);
    if (!row) return { updated: false };
    const result = db.prepare(
      "UPDATE sessions SET end_time = CURRENT_TIMESTAMP, status = 'completed' WHERE id = ?"
    ).run(sessionId);
    return { updated: result.changes > 0 };
  },

  endActiveSessionsForCamera(cameraId) {
    db.prepare(
      "UPDATE sessions SET end_time = CURRENT_TIMESTAMP, status = 'completed' WHERE camera_id = ? AND status = 'active'"
    ).run(cameraId);
  },

  findActiveByCamera(cameraId) {
    return db.prepare(
      "SELECT * FROM sessions WHERE camera_id = ? AND status = 'active' ORDER BY start_time DESC LIMIT 1"
    ).get(cameraId);
  },

  findByUserId(userId, limit = 20, offset = 0) {
    const sessions = db.prepare(`
      SELECT s.*, c.name as camera_name
      FROM sessions s
      JOIN cameras c ON s.camera_id = c.id
      WHERE c.user_id = ?
      ORDER BY s.start_time DESC
      LIMIT ? OFFSET ?
    `).all(userId, limit, offset);

    const countRow = db.prepare(`
      SELECT COUNT(*) as total
      FROM sessions s
      JOIN cameras c ON s.camera_id = c.id
      WHERE c.user_id = ?
    `).get(userId);

    return { sessions, total: countRow.total };
  },

  getStatsForUser(userId) {
    const row = db.prepare(`
      SELECT
        COUNT(*) as total,
        SUM(CASE WHEN s.status = 'active' THEN 1 ELSE 0 END) as active,
        SUM(CASE WHEN date(s.start_time) = date('now') THEN 1 ELSE 0 END) as today
      FROM sessions s
      JOIN cameras c ON s.camera_id = c.id
      WHERE c.user_id = ?
    `).get(userId);

    return {
      total: row.total || 0,
      active: row.active || 0,
      today: row.today || 0,
    };
  },
};

module.exports = Session;
