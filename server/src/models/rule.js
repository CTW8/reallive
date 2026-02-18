const db = require('./db');

const Rule = {
  findByUserId(userId, filters = {}) {
    let sql = 'SELECT * FROM alert_rules WHERE user_id = ?';
    const params = [userId];

    if (filters.priority && filters.priority !== 'all') {
      sql += ' AND priority = ?';
      params.push(filters.priority);
    }

    if (filters.enabled !== undefined && filters.enabled !== 'all') {
      sql += ' AND enabled = ?';
      params.push(filters.enabled === 'enabled' ? 1 : 0);
    }

    if (filters.escalation && filters.escalation !== 'all') {
      sql += ' AND escalation = ?';
      params.push(filters.escalation);
    }

    if (filters.query) {
      sql += ' AND name LIKE ?';
      params.push(`%${filters.query}%`);
    }

    const sortField = filters.sortBy || 'priority';
    const sortOrder = filters.order || 'asc';
    const validSorts = ['priority', 'name', 'escalation'];
    const validOrders = ['asc', 'desc'];
    const safeSort = validSorts.includes(sortField) ? sortField : 'priority';
    const safeOrder = validOrders.includes(sortOrder) ? sortOrder : 'asc';

    const priorityOrder = safeSort === 'priority' 
      ? `CASE priority WHEN 'high' THEN 1 WHEN 'medium' THEN 2 ELSE 3 END ${safeOrder.toUpperCase()}, `
      : '';
    
    sql += ` ORDER BY ${priorityOrder}created_at DESC`;

    return db.prepare(sql).all(...params);
  },

  findById(id, userId) {
    return db.prepare('SELECT * FROM alert_rules WHERE id = ? AND user_id = ?').get(id, userId);
  },

  findByName(name, userId, excludeId = null) {
    let sql = 'SELECT * FROM alert_rules WHERE LOWER(name) = LOWER(?) AND user_id = ?';
    const params = [name, userId];

    if (excludeId) {
      sql += ' AND id != ?';
      params.push(excludeId);
    }

    return db.prepare(sql).get(...params);
  },

  create(userId, data) {
    const stmt = db.prepare(`
      INSERT INTO alert_rules (user_id, name, priority, condition, actions, escalation, quiet_hours, enabled)
      VALUES (?, ?, ?, ?, ?, ?, ?, ?)
    `);
    const result = stmt.run(
      userId,
      data.name,
      data.priority || 'medium',
      data.condition,
      data.actions,
      data.escalation || 'Immediately',
      data.quiet_hours || 'Disabled',
      data.enabled !== undefined ? (data.enabled ? 1 : 0) : 1
    );
    return this.findById(result.lastInsertRowid, userId);
  },

  update(id, userId, data) {
    const fields = [];
    const params = [];

    if (data.name !== undefined) {
      fields.push('name = ?');
      params.push(data.name);
    }
    if (data.priority !== undefined) {
      fields.push('priority = ?');
      params.push(data.priority);
    }
    if (data.condition !== undefined) {
      fields.push('condition = ?');
      params.push(data.condition);
    }
    if (data.actions !== undefined) {
      fields.push('actions = ?');
      params.push(data.actions);
    }
    if (data.escalation !== undefined) {
      fields.push('escalation = ?');
      params.push(data.escalation);
    }
    if (data.quiet_hours !== undefined) {
      fields.push('quiet_hours = ?');
      params.push(data.quiet_hours);
    }
    if (data.enabled !== undefined) {
      fields.push('enabled = ?');
      params.push(data.enabled ? 1 : 0);
    }

    if (fields.length === 0) return this.findById(id, userId);

    params.push(id, userId);
    const sql = `UPDATE alert_rules SET ${fields.join(', ')} WHERE id = ? AND user_id = ?`;
    db.prepare(sql).run(...params);
    return this.findById(id, userId);
  },

  setEnabled(id, userId, enabled) {
    return this.update(id, userId, { enabled });
  },

  delete(id, userId) {
    db.prepare('DELETE FROM alert_rules WHERE id = ? AND user_id = ?').run(id, userId);
  },

  batchUpdate(ids, userId, data) {
    const fields = [];
    const params = [];

    if (data.enabled !== undefined) {
      fields.push('enabled = ?');
      params.push(data.enabled ? 1 : 0);
    }

    if (fields.length === 0) return;

    const placeholders = ids.map(() => '?').join(',');
    params.push(...ids, userId);
    const sql = `UPDATE alert_rules SET ${fields.join(', ')} WHERE id IN (${placeholders}) AND user_id = ?`;
    db.prepare(sql).run(...params);
  },

  batchDelete(ids, userId) {
    const placeholders = ids.map(() => '?').join(',');
    const sql = `DELETE FROM alert_rules WHERE id IN (${placeholders}) AND user_id = ?`;
    db.prepare(sql).run(...ids, userId);
  },
};

module.exports = Rule;
