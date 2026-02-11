const db = require('./db');

const User = {
  create(username, passwordHash, email) {
    const stmt = db.prepare(
      'INSERT INTO users (username, password_hash, email) VALUES (?, ?, ?)'
    );
    const result = stmt.run(username, passwordHash, email);
    return result.lastInsertRowid;
  },

  findByUsername(username) {
    return db.prepare('SELECT * FROM users WHERE username = ?').get(username);
  },

  findById(id) {
    return db.prepare('SELECT id, username, email, created_at FROM users WHERE id = ?').get(id);
  },
};

module.exports = User;
