const express = require('express');
const bcrypt = require('bcryptjs');
const authMiddleware = require('../middleware/auth');
const config = require('../config');
const db = require('../models/db');
const User = require('../models/user');
const UserSettings = require('../models/user-settings');

const router = express.Router();

const DEFAULT_SETTINGS = Object.freeze({
  profile: {
    phone: '',
    role: 'Administrator',
    signature: '',
    language: 'English',
    timezone: 'UTC+08:00',
  },
  notifications: {
    email: true,
    sms: false,
    webhook: false,
    quietHours: 'Disabled',
    escalationDelay: 'After 60 seconds',
    escalationRule: '',
  },
  system: {
    nvrMode: 'Event Priority',
    networkProbe: true,
  },
  security: {
    twoFactor: false,
    trustedDevice: true,
    ipAllowlist: false,
  },
});

function safeString(value, fallback = '') {
  if (value == null) return fallback;
  return String(value).trim();
}

function normalizeRole(raw) {
  const v = safeString(raw, '').toLowerCase();
  if (v === 'admin' || v === 'administrator') return 'admin';
  if (v === 'operator') return 'operator';
  return 'viewer';
}

function roleLabel(raw) {
  const role = normalizeRole(raw);
  if (role === 'admin') return 'Administrator';
  if (role === 'operator') return 'Operator';
  return 'Viewer';
}

function isAdmin(user) {
  return normalizeRole(user?.role) === 'admin';
}

function requireAdmin(req, res) {
  const me = User.findAuthById(req.user.id);
  if (!me) {
    res.status(404).json({ error: 'User not found' });
    return null;
  }
  if (!isAdmin(me)) {
    res.status(403).json({ error: 'Administrator role required' });
    return null;
  }
  return me;
}

function pushAudit(userId, action, type = 'config') {
  db.prepare(
    'INSERT INTO audit_logs (user_id, action, type) VALUES (?, ?, ?)'
  ).run(userId, safeString(action, 'Updated settings'), safeString(type, 'config'));
}

function readAuditLogs(userId, limit = 40) {
  const rows = db.prepare(`
    SELECT a.id, a.action, a.type, a.created_at, u.username
    FROM audit_logs a
    LEFT JOIN users u ON u.id = a.user_id
    WHERE a.user_id = ?
    ORDER BY a.created_at DESC
    LIMIT ?
  `).all(userId, Math.max(1, Math.min(200, Number(limit) || 40)));
  return rows.map((row) => ({
    id: row.id,
    action: row.action,
    type: row.type || 'config',
    user: row.username || 'Unknown',
    time: row.created_at,
  }));
}

function buildSettingsPayload(user, stored) {
  const profileStored = stored?.profile || {};
  return {
    profile: {
      displayName: user?.username || '',
      email: user?.email || '',
      phone: safeString(profileStored.phone, DEFAULT_SETTINGS.profile.phone),
      role: roleLabel(user?.role),
      signature: safeString(profileStored.signature, DEFAULT_SETTINGS.profile.signature),
      language: safeString(profileStored.language, DEFAULT_SETTINGS.profile.language),
      timezone: safeString(profileStored.timezone, DEFAULT_SETTINGS.profile.timezone),
    },
    notifications: { ...DEFAULT_SETTINGS.notifications, ...(stored?.notifications || {}) },
    system: { ...DEFAULT_SETTINGS.system, ...(stored?.system || {}) },
    security: { ...DEFAULT_SETTINGS.security, ...(stored?.security || {}) },
    updatedAt: stored?.updatedAt || null,
  };
}

router.use(authMiddleware);

router.get('/', (req, res) => {
  const user = User.findById(req.user.id);
  if (!user) return res.status(404).json({ error: 'User not found' });
  const stored = UserSettings.getByUserId(req.user.id);
  return res.json({
    ...buildSettingsPayload(user, stored),
    auditLogs: readAuditLogs(req.user.id, 50),
  });
});

router.put('/profile', (req, res) => {
  const user = User.findById(req.user.id);
  if (!user) return res.status(404).json({ error: 'User not found' });

  const displayName = safeString(req.body?.displayName);
  const email = safeString(req.body?.email).toLowerCase();
  if (!displayName || !email) {
    return res.status(400).json({ error: 'displayName and email are required' });
  }

  const existsUsername = User.findByUsername(displayName);
  if (existsUsername && Number(existsUsername.id) !== Number(req.user.id)) {
    return res.status(409).json({ error: 'Username already taken' });
  }
  const existsEmail = User.findByEmail(email);
  if (existsEmail && Number(existsEmail.id) !== Number(req.user.id)) {
    return res.status(409).json({ error: 'Email already registered' });
  }

  const updatedUser = User.updateBasicById(req.user.id, displayName, email);
  const stored = UserSettings.upsert(req.user.id, {
    profile: {
      phone: safeString(req.body?.phone),
      signature: safeString(req.body?.signature),
      language: safeString(req.body?.language, DEFAULT_SETTINGS.profile.language),
      timezone: safeString(req.body?.timezone, DEFAULT_SETTINGS.profile.timezone),
    },
  });
  pushAudit(req.user.id, 'Updated account profile', 'security');
  return res.json(buildSettingsPayload(updatedUser, stored));
});

router.put('/preferences', (req, res) => {
  const notifications = req.body?.notifications || {};
  const system = req.body?.system || {};
  const security = req.body?.security || {};
  const stored = UserSettings.upsert(req.user.id, { notifications, system, security });
  const user = User.findById(req.user.id);
  pushAudit(req.user.id, 'Updated notification/system preferences', 'config');
  return res.json(buildSettingsPayload(user, stored));
});

router.post('/password', (req, res) => {
  const currentPassword = String(req.body?.currentPassword || '');
  const newPassword = String(req.body?.newPassword || '');
  if (!currentPassword || !newPassword) {
    return res.status(400).json({ error: 'currentPassword and newPassword are required' });
  }
  if (newPassword.length < 6) {
    return res.status(400).json({ error: 'Password must be at least 6 characters' });
  }

  const user = User.findAuthById(req.user.id);
  if (!user) return res.status(404).json({ error: 'User not found' });
  const valid = bcrypt.compareSync(currentPassword, user.password_hash);
  if (!valid) return res.status(400).json({ error: 'Current password is incorrect' });

  const passwordHash = bcrypt.hashSync(newPassword, config.bcryptRounds);
  User.updatePasswordById(req.user.id, passwordHash);
  pushAudit(req.user.id, 'Changed account password', 'security');
  return res.json({ ok: true });
});

router.get('/audit', (req, res) => {
  const limit = Number(req.query?.limit || 40);
  return res.json({ rows: readAuditLogs(req.user.id, limit) });
});

router.get('/users', (req, res) => {
  const me = requireAdmin(req, res);
  if (!me) return;
  const limit = Math.max(1, Math.min(500, Number(req.query?.limit || 200)));
  const offset = Math.max(0, Number(req.query?.offset || 0));
  const rows = User.listAll(limit, offset).map((row) => ({
    id: row.id,
    username: row.username,
    email: row.email,
    role: normalizeRole(row.role),
    createdAt: row.created_at,
  }));
  const total = Number(db.prepare('SELECT COUNT(*) as c FROM users').get()?.c || 0);
  return res.json({ rows, total, limit, offset });
});

router.post('/users', (req, res) => {
  const me = requireAdmin(req, res);
  if (!me) return;
  const username = safeString(req.body?.username);
  const email = safeString(req.body?.email).toLowerCase();
  const password = String(req.body?.password || '');
  const role = normalizeRole(req.body?.role);
  if (!username || !email || !password) {
    return res.status(400).json({ error: 'username, email, and password are required' });
  }
  if (password.length < 6) {
    return res.status(400).json({ error: 'Password must be at least 6 characters' });
  }
  if (User.findByUsername(username)) {
    return res.status(409).json({ error: 'Username already taken' });
  }
  if (User.findByEmail(email)) {
    return res.status(409).json({ error: 'Email already registered' });
  }
  const passwordHash = bcrypt.hashSync(password, config.bcryptRounds);
  const id = User.create(username, passwordHash, email, role);
  pushAudit(req.user.id, `Created user ${username} (${role})`, 'security');
  const row = User.findById(id);
  return res.status(201).json({
    id: row.id,
    username: row.username,
    email: row.email,
    role: normalizeRole(row.role),
    createdAt: row.created_at,
  });
});

router.patch('/users/:id', (req, res) => {
  const me = requireAdmin(req, res);
  if (!me) return;
  const targetId = Number(req.params.id);
  if (!Number.isFinite(targetId) || targetId <= 0) {
    return res.status(400).json({ error: 'Invalid user id' });
  }
  const target = User.findAuthById(targetId);
  if (!target) return res.status(404).json({ error: 'User not found' });

  const username = req.body?.username != null ? safeString(req.body.username) : undefined;
  const email = req.body?.email != null ? safeString(req.body.email).toLowerCase() : undefined;
  const role = req.body?.role != null ? normalizeRole(req.body.role) : undefined;

  if (username !== undefined && !username) {
    return res.status(400).json({ error: 'username cannot be empty' });
  }
  if (email !== undefined && !email) {
    return res.status(400).json({ error: 'email cannot be empty' });
  }

  if (username) {
    const existsUser = User.findByUsername(username);
    if (existsUser && Number(existsUser.id) !== targetId) {
      return res.status(409).json({ error: 'Username already taken' });
    }
  }
  if (email) {
    const existsEmail = User.findByEmail(email);
    if (existsEmail && Number(existsEmail.id) !== targetId) {
      return res.status(409).json({ error: 'Email already registered' });
    }
  }

  const updated = User.updateById(targetId, {
    username,
    email,
    role,
  });
  pushAudit(req.user.id, `Updated user ${updated.username}`, 'security');
  return res.json({
    id: updated.id,
    username: updated.username,
    email: updated.email,
    role: normalizeRole(updated.role),
    createdAt: updated.created_at,
  });
});

router.delete('/users/:id', (req, res) => {
  const me = requireAdmin(req, res);
  if (!me) return;
  const targetId = Number(req.params.id);
  if (!Number.isFinite(targetId) || targetId <= 0) {
    return res.status(400).json({ error: 'Invalid user id' });
  }
  if (targetId === Number(req.user.id)) {
    return res.status(400).json({ error: 'Cannot delete current login user' });
  }
  const target = User.findAuthById(targetId);
  if (!target) return res.status(404).json({ error: 'User not found' });
  User.deleteById(targetId);
  pushAudit(req.user.id, `Deleted user ${target.username}`, 'security');
  return res.json({ ok: true });
});

module.exports = router;
