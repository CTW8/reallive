const db = require('./db');

const Alert = {
  findByUserId(userId, filters = {}) {
    let sql = `
      SELECT a.*, c.name as camera_name
      FROM alerts a
      LEFT JOIN cameras c ON a.camera_id = c.id
      WHERE a.user_id = ?
    `;
    const params = [userId];

    if (filters.type && filters.type !== 'all') {
      sql += ' AND a.type = ?';
      params.push(filters.type);
    }

    if (filters.status && filters.status !== 'all') {
      sql += ' AND a.status = ?';
      params.push(filters.status);
    }

    sql += ' ORDER BY a.created_at DESC';

    if (filters.limit) {
      sql += ' LIMIT ?';
      params.push(filters.limit);
    }

    return db.prepare(sql).all(...params);
  },

  findById(id, userId) {
    const sql = `
      SELECT a.*, c.name as camera_name
      FROM alerts a
      LEFT JOIN cameras c ON a.camera_id = c.id
      WHERE a.id = ? AND a.user_id = ?
    `;
    return db.prepare(sql).get(id, userId);
  },

  create(userId, data) {
    const stmt = db.prepare(`
      INSERT INTO alerts (user_id, camera_id, type, title, description, status)
      VALUES (?, ?, ?, ?, ?, ?)
    `);
    const result = stmt.run(
      userId,
      data.camera_id || null,
      data.type,
      data.title,
      data.description || '',
      data.status || 'new'
    );
    return this.findById(result.lastInsertRowid, userId);
  },

  update(id, userId, data) {
    const fields = [];
    const params = [];

    if (data.status !== undefined) {
      fields.push('status = ?');
      params.push(data.status);
    }
    if (data.resolved_at !== undefined) {
      fields.push('resolved_at = ?');
      params.push(data.resolved_at);
    }

    if (fields.length === 0) return this.findById(id, userId);

    params.push(id, userId);
    const sql = `UPDATE alerts SET ${fields.join(', ')} WHERE id = ? AND user_id = ?`;
    db.prepare(sql).run(...params);
    return this.findById(id, userId);
  },

  markRead(id, userId) {
    return this.update(id, userId, { status: 'read' });
  },

  resolve(id, userId) {
    return this.update(id, userId, { 
      status: 'resolved', 
      resolved_at: new Date().toISOString() 
    });
  },

  batchUpdate(ids, userId, status) {
    const placeholders = ids.map(() => '?').join(',');
    const sql = `UPDATE alerts SET status = ? WHERE id IN (${placeholders}) AND user_id = ?`;
    db.prepare(sql).run(status, ...ids, userId);
  },

  delete(id, userId) {
    db.prepare('DELETE FROM alerts WHERE id = ? AND user_id = ?').run(id, userId);
  },

  batchDelete(ids, userId) {
    const placeholders = ids.map(() => '?').join(',');
    const sql = `DELETE FROM alerts WHERE id IN (${placeholders}) AND user_id = ?`;
    db.prepare(sql).run(...ids, userId);
  },

  getStats(userId) {
    const sql = `
      SELECT 
        COUNT(*) as total,
        SUM(CASE WHEN status = 'new' THEN 1 ELSE 0 END) as new_count,
        SUM(CASE WHEN status = 'read' THEN 1 ELSE 0 END) as read_count,
        SUM(CASE WHEN status = 'resolved' THEN 1 ELSE 0 END) as resolved_count
      FROM alerts
      WHERE user_id = ?
    `;
    return db.prepare(sql).get(userId);
  },

  getUnreadCount(userId) {
    const sql = 'SELECT COUNT(*) as count FROM alerts WHERE user_id = ? AND status = ?';
    return db.prepare(sql).get(userId, 'new').count;
  },
};

module.exports = Alert;
