const db = require('./db');

const Camera = {
  create(userId, name, streamKey, resolution) {
    const stmt = db.prepare(
      'INSERT INTO cameras (user_id, name, stream_key, resolution) VALUES (?, ?, ?, ?)'
    );
    const result = stmt.run(userId, name, streamKey, resolution || '1080p');
    return this.findById(result.lastInsertRowid);
  },

  findById(id) {
    return db.prepare('SELECT * FROM cameras WHERE id = ?').get(id);
  },

  findByUserId(userId) {
    return db.prepare('SELECT * FROM cameras WHERE user_id = ? ORDER BY created_at DESC').all(userId);
  },

  findByStreamKey(streamKey) {
    return db.prepare('SELECT * FROM cameras WHERE stream_key = ?').get(streamKey);
  },

  update(id, userId, fields) {
    const allowed = ['name', 'resolution', 'status'];
    const updates = [];
    const values = [];
    for (const key of allowed) {
      if (fields[key] !== undefined) {
        updates.push(`${key} = ?`);
        values.push(fields[key]);
      }
    }
    if (updates.length === 0) return this.findById(id);
    values.push(id, userId);
    db.prepare(
      `UPDATE cameras SET ${updates.join(', ')} WHERE id = ? AND user_id = ?`
    ).run(...values);
    return this.findById(id);
  },

  updateStatus(id, status) {
    db.prepare('UPDATE cameras SET status = ? WHERE id = ?').run(status, id);
  },

  delete(id, userId) {
    const result = db.prepare('DELETE FROM cameras WHERE id = ? AND user_id = ?').run(id, userId);
    return result.changes > 0;
  },
};

module.exports = Camera;
