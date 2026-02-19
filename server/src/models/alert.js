const db = require('./db');

function normalizeEventTsMs(rawTs) {
  const n = Number(rawTs);
  if (!Number.isFinite(n) || n <= 0) return Date.now();
  if (n > 1e15) return Math.floor(n / 1000); // us
  if (n > 1e18) return Math.floor(n / 1000000); // ns
  if (n < 1e12) return Math.floor(n * 1000); // s
  return Math.floor(n); // ms
}

function buildWhereClause(userId, filters = {}, params = []) {
  let where = ' WHERE a.user_id = ?';
  params.push(userId);

  if (filters.type && filters.type !== 'all') {
    where += ' AND a.type = ?';
    params.push(filters.type);
  }

  if (filters.status && filters.status !== 'all') {
    where += ' AND a.status = ?';
    params.push(filters.status);
  }

  if (filters.typeGroup && filters.typeGroup !== 'all') {
    if (filters.typeGroup === 'motion') {
      where += " AND (LOWER(a.type) LIKE '%motion%' OR LOWER(a.type) LIKE '%person%')";
    } else if (filters.typeGroup === 'offline') {
      where += " AND (LOWER(a.type) LIKE '%offline%' OR LOWER(a.type) LIKE '%disconnect%')";
    } else if (filters.typeGroup === 'alarm') {
      where += " AND (LOWER(a.type) LIKE '%alarm%' OR LOWER(a.type) LIKE '%intrusion%' OR LOWER(a.type) LIKE '%warning%')";
    } else if (filters.typeGroup === 'system') {
      where += " AND (LOWER(a.type) NOT LIKE '%motion%' AND LOWER(a.type) NOT LIKE '%person%' AND LOWER(a.type) NOT LIKE '%offline%' AND LOWER(a.type) NOT LIKE '%disconnect%' AND LOWER(a.type) NOT LIKE '%alarm%' AND LOWER(a.type) NOT LIKE '%intrusion%' AND LOWER(a.type) NOT LIKE '%warning%')";
    }
  }

  if (filters.since) {
    where += ' AND datetime(a.created_at) >= datetime(?)';
    params.push(filters.since);
  }

  if (filters.until) {
    where += ' AND datetime(a.created_at) <= datetime(?)';
    params.push(filters.until);
  }

  if (filters.q) {
    where += ' AND (LOWER(a.title) LIKE ? OR LOWER(a.description) LIKE ? OR LOWER(a.type) LIKE ? OR LOWER(c.name) LIKE ?)';
    const like = `%${String(filters.q).toLowerCase()}%`;
    params.push(like, like, like, like);
  }

  return where;
}

const Alert = {
  findByUserId(userId, filters = {}) {
    const params = [];
    const where = buildWhereClause(userId, filters, params);
    let sql = `
      SELECT a.*, c.name as camera_name
      FROM alerts a
      LEFT JOIN cameras c ON a.camera_id = c.id
    ` + where;

    sql += ' ORDER BY a.created_at DESC';

    if (filters.limit) {
      sql += ' LIMIT ?';
      params.push(filters.limit);
    }

    return db.prepare(sql).all(...params);
  },

  findByUserIdPaged(userId, filters = {}) {
    const safeLimit = Math.max(1, Math.min(200, Number(filters.limit) || 20));
    const safeOffset = Math.max(0, Number(filters.offset) || 0);

    const listParams = [];
    const where = buildWhereClause(userId, filters, listParams);
    const listSql = `
      SELECT a.*, c.name as camera_name
      FROM alerts a
      LEFT JOIN cameras c ON a.camera_id = c.id
      ${where}
      ORDER BY a.created_at DESC
      LIMIT ? OFFSET ?
    `;
    const items = db.prepare(listSql).all(...listParams, safeLimit, safeOffset);

    const countParams = [];
    const countWhere = buildWhereClause(userId, filters, countParams);
    const countSql = `
      SELECT COUNT(*) as total
      FROM alerts a
      LEFT JOIN cameras c ON a.camera_id = c.id
      ${countWhere}
    `;
    const total = Number(db.prepare(countSql).get(...countParams)?.total || 0);

    return {
      items,
      total,
      limit: safeLimit,
      offset: safeOffset,
    };
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

  createPersonDetectedEvent(userId, camera, event = {}) {
    const cameraId = Number(camera?.id || 0) || null;
    const cameraName = String(camera?.name || 'Camera');
    const ts = normalizeEventTsMs(event.ts || Date.now());
    const score = Number(event.score || 0);
    const bbox = event.bbox && typeof event.bbox === 'object' ? event.bbox : {};
    const x = Math.max(0, Math.floor(Number(bbox.x || 0)));
    const y = Math.max(0, Math.floor(Number(bbox.y || 0)));
    const w = Math.max(0, Math.floor(Number(bbox.w || 0)));
    const h = Math.max(0, Math.floor(Number(bbox.h || 0)));
    const marker = `[evt:${Math.floor(ts)}:${x}:${y}:${w}:${h}]`;

    const existing = db.prepare(`
      SELECT id
      FROM alerts
      WHERE user_id = ? AND camera_id IS ? AND type = 'person-detected' AND description LIKE ?
      LIMIT 1
    `).get(userId, cameraId, `%${marker}%`);
    if (existing) return null;

    const scorePct = Math.round(Math.max(0, Math.min(1, score)) * 100);
    const boxText = (w > 0 && h > 0) ? ` bbox=${x},${y},${w}x${h}` : '';
    const tsSec = Math.max(0, Math.floor(ts / 1000));
    const stmt = db.prepare(`
      INSERT INTO alerts (user_id, camera_id, type, title, description, status, created_at)
      VALUES (?, ?, ?, ?, ?, ?, datetime(?, 'unixepoch'))
    `);
    const result = stmt.run(
      userId,
      cameraId,
      'person-detected',
      'Person Detected',
      `Person detected on ${cameraName} (score ${scorePct}%).${boxText} ${marker}`.trim(),
      'new',
      tsSec
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
