const express = require('express');
const bcrypt = require('bcryptjs');
const crypto = require('crypto');
const config = require('../config');
const User = require('../models/user');
const db = require('../models/db');
const AuthSession = require('../models/auth-session');
const authMiddleware = require('../middleware/auth');
const {
  issueAccessToken,
  issueRefreshToken,
  verifyToken,
  hashRefreshToken,
  getRefreshExpiryIso,
} = require('../services/auth-token-service');

const router = express.Router();

function getClientMeta(req) {
  return {
    deviceName: String(req.body?.deviceName || req.headers['x-device-name'] || '').trim(),
    platform: String(req.body?.platform || req.headers['x-platform'] || '').trim(),
    appVersion: String(req.body?.appVersion || req.headers['x-app-version'] || '').trim(),
    ipAddress: String(req.ip || ''),
    userAgent: String(req.headers['user-agent'] || ''),
  };
}

function buildAuthPayload(user, req) {
  const refreshToken = issueRefreshToken({ userId: user.id, sessionId: -1 });
  const refreshHash = hashRefreshToken(refreshToken);
  const session = AuthSession.create({
    userId: user.id,
    refreshTokenHash: refreshHash,
    expiresAtIso: getRefreshExpiryIso(),
    ...getClientMeta(req),
  });
  const boundRefresh = issueRefreshToken({ userId: user.id, sessionId: session.id });
  const boundRefreshHash = hashRefreshToken(boundRefresh);
  AuthSession.rotateRefreshToken(session.id, boundRefreshHash, getRefreshExpiryIso());
  const accessToken = issueAccessToken({ userId: user.id, username: user.username, sessionId: session.id });
  return {
    token: accessToken,
    refreshToken: boundRefresh,
    user: { id: user.id, username: user.username, email: user.email },
    session: {
      id: session.id,
      expiresAt: session.expires_at,
    },
  };
}

// POST /api/auth/register
router.post('/register', (req, res) => {
  const { username, password, email } = req.body;
  const normalizedUsername = String(username || '').trim();
  const normalizedEmail = String(email || '').trim().toLowerCase();

  if (!normalizedUsername || !password || !normalizedEmail) {
    return res.status(400).json({ error: 'username, password, and email are required' });
  }
  if (password.length < 8) {
    return res.status(400).json({ error: 'Password must be at least 8 characters' });
  }

  const existing = User.findByUsername(normalizedUsername);
  if (existing) {
    return res.status(409).json({ error: 'Username already taken' });
  }
  const existingEmail = User.findByEmail(normalizedEmail);
  if (existingEmail) {
    return res.status(409).json({ error: 'Email already registered' });
  }

  try {
    const passwordHash = bcrypt.hashSync(password, config.bcryptRounds);
    const userId = User.create(normalizedUsername, passwordHash, normalizedEmail);
    const created = { id: userId, username: normalizedUsername, email: normalizedEmail };
    res.status(201).json(buildAuthPayload(created, req));
  } catch (err) {
    if (err.message && err.message.includes('UNIQUE constraint failed')) {
      return res.status(409).json({ error: 'Username or email already taken' });
    }
    res.status(500).json({ error: 'Registration failed' });
  }
});

// POST /api/auth/login
router.post('/login', (req, res) => {
  const { username, password } = req.body;
  const identifier = String(username || '').trim();

  if (!identifier || !password) {
    return res.status(400).json({ error: 'username and password are required' });
  }

  const user = User.findByUsernameOrEmail(identifier);
  if (!user) {
    return res.status(401).json({ error: 'Invalid credentials' });
  }

  const valid = bcrypt.compareSync(password, user.password_hash);
  if (!valid) {
    return res.status(401).json({ error: 'Invalid credentials' });
  }

  res.json(buildAuthPayload(user, req));
});

// POST /api/auth/refresh
router.post('/refresh', (req, res) => {
  const refreshToken = String(req.body?.refreshToken || '').trim();
  if (!refreshToken) {
    return res.status(400).json({ error: 'refreshToken is required' });
  }
  try {
    const payload = verifyToken(refreshToken);
    if (payload?.typ !== 'refresh' || !payload?.sid || !payload?.id) {
      return res.status(401).json({ error: 'Invalid refresh token' });
    }
    const sessionId = Number(payload.sid);
    const session = AuthSession.findById(sessionId);
    if (!AuthSession.isActive(session)) {
      return res.status(401).json({ error: 'Session expired' });
    }
    if (Number(session.user_id) !== Number(payload.id)) {
      return res.status(401).json({ error: 'Session mismatch' });
    }
    const incomingHash = hashRefreshToken(refreshToken);
    if (incomingHash !== session.refresh_token_hash) {
      AuthSession.revoke(sessionId);
      return res.status(401).json({ error: 'Refresh token mismatch' });
    }
    const user = User.findAuthById(Number(payload.id));
    if (!user) {
      AuthSession.revoke(sessionId);
      return res.status(401).json({ error: 'User not found' });
    }
    const nextRefresh = issueRefreshToken({ userId: user.id, sessionId });
    const nextRefreshHash = hashRefreshToken(nextRefresh);
    AuthSession.rotateRefreshToken(sessionId, nextRefreshHash, getRefreshExpiryIso());
    AuthSession.touch(sessionId, req.ip, req.headers['user-agent'] || '');
    const accessToken = issueAccessToken({ userId: user.id, username: user.username, sessionId });
    return res.json({
      token: accessToken,
      refreshToken: nextRefresh,
      user: { id: user.id, username: user.username, email: user.email },
      session: { id: sessionId },
    });
  } catch (_err) {
    return res.status(401).json({ error: 'Invalid refresh token' });
  }
});

// POST /api/auth/forgot-password
router.post('/forgot-password', (req, res) => {
  const email = String(req.body?.email || '').trim().toLowerCase();
  if (!email) {
    return res.status(400).json({ error: 'email is required' });
  }

  const user = User.findByEmail(email);
  if (user) {
    const resetNonce = crypto.randomBytes(24).toString('base64url');
    console.log('[Auth] Forgot-password requested for user', user.id, 'nonce', resetNonce);
  }

  return res.json({
    message: 'If the email exists, reset instructions have been sent',
  });
});

// DELETE /api/auth/me
router.delete('/me', authMiddleware, (req, res) => {
  const userId = Number(req.user?.id || 0);
  if (!userId) {
    return res.status(401).json({ error: 'Unauthorized' });
  }
  AuthSession.revokeByUserId(userId);
  const result = User.deleteById(userId);
  if (!result?.changes) {
    return res.status(404).json({ error: 'User not found' });
  }
  return res.json({ ok: true });
});

// POST /api/auth/logout
router.post('/logout', authMiddleware, (req, res) => {
  const sessionId = Number(req.user?.sessionId || 0);
  if (sessionId > 0) {
    AuthSession.revoke(sessionId);
  }
  return res.json({ ok: true });
});

// GET /api/auth/sessions
router.get('/sessions', authMiddleware, (req, res) => {
  const rows = db.prepare(`
    SELECT id, device_name, platform, app_version, ip_address, user_agent,
           last_seen_at, expires_at, revoked_at, created_at
    FROM auth_sessions
    WHERE user_id = ?
    ORDER BY created_at DESC
    LIMIT 50
  `).all(req.user.id);
  const currentSessionId = Number(req.user?.sessionId || 0);
  return res.json({
    sessions: rows.map((row) => ({
      ...row,
      current: row.id === currentSessionId,
      active: AuthSession.isActive(row),
    })),
  });
});

// POST /api/auth/sessions/:id/revoke
router.post('/sessions/:id/revoke', authMiddleware, (req, res) => {
  const sessionId = Number(req.params.id);
  if (!Number.isFinite(sessionId) || sessionId <= 0) {
    return res.status(400).json({ error: 'Invalid session id' });
  }
  const row = db.prepare(
    'SELECT id, user_id FROM auth_sessions WHERE id = ? LIMIT 1'
  ).get(sessionId);
  if (!row || Number(row.user_id) !== Number(req.user.id)) {
    return res.status(404).json({ error: 'Session not found' });
  }
  AuthSession.revoke(sessionId);
  return res.json({ ok: true });
});

// POST /api/auth/sessions/revoke-others
router.post('/sessions/revoke-others', authMiddleware, (req, res) => {
  const currentSessionId = Number(req.user?.sessionId || 0);
  if (!currentSessionId) {
    return res.status(400).json({ error: 'Invalid current session' });
  }
  const result = db.prepare(`
    UPDATE auth_sessions
    SET revoked_at = CURRENT_TIMESTAMP
    WHERE user_id = ? AND id != ? AND revoked_at IS NULL
  `).run(req.user.id, currentSessionId);
  return res.json({ ok: true, affected: Number(result?.changes || 0) });
});

module.exports = router;
