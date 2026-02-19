const db = require('./db');

const User = {
  create(username, passwordHash, email, role = 'viewer') {
    const stmt = db.prepare(
      'INSERT INTO users (username, password_hash, email, role) VALUES (?, ?, ?, ?)'
    );
    const result = stmt.run(username, passwordHash, email, role);
    return result.lastInsertRowid;
  },

  findByUsername(username) {
    return db.prepare('SELECT * FROM users WHERE username = ?').get(username);
  },

  findByEmail(email) {
    return db.prepare('SELECT * FROM users WHERE lower(email) = lower(?)').get(email);
  },

  findByUsernameOrEmail(identifier) {
    return db
      .prepare('SELECT * FROM users WHERE username = ? OR lower(email) = lower(?) LIMIT 1')
      .get(identifier, identifier);
  },

  findById(id) {
    return db.prepare('SELECT id, username, email, role, created_at FROM users WHERE id = ?').get(id);
  },

  findAuthById(id) {
    return db.prepare('SELECT * FROM users WHERE id = ?').get(id);
  },

  updatePasswordById(id, passwordHash) {
    db.prepare('UPDATE users SET password_hash = ? WHERE id = ?').run(passwordHash, id);
  },

  updateBasicById(id, username, email) {
    db.prepare('UPDATE users SET username = ?, email = ? WHERE id = ?').run(username, email, id);
    return this.findById(id);
  },

  listAll(limit = 200, offset = 0) {
    return db.prepare(
      'SELECT id, username, email, role, created_at FROM users ORDER BY id ASC LIMIT ? OFFSET ?'
    ).all(limit, offset);
  },

  updateById(id, data = {}) {
    const current = this.findAuthById(id);
    if (!current) return null;
    const username = data.username != null ? String(data.username).trim() : current.username;
    const email = data.email != null ? String(data.email).trim().toLowerCase() : current.email;
    const role = data.role != null ? String(data.role).trim().toLowerCase() : current.role;
    db.prepare('UPDATE users SET username = ?, email = ?, role = ? WHERE id = ?').run(username, email, role, id);
    return this.findById(id);
  },

  deleteById(id) {
    return db.prepare('DELETE FROM users WHERE id = ?').run(id);
  },
};

module.exports = User;
