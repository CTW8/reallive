const crypto = require('crypto');
const jwt = require('jsonwebtoken');
const config = require('../config');

const ACCESS_EXPIRES_IN = process.env.JWT_ACCESS_EXPIRES_IN || '12h';
const REFRESH_EXPIRES_DAYS = Number(process.env.JWT_REFRESH_EXPIRES_DAYS || 30);

function issueAccessToken({ userId, username, sessionId }) {
  return jwt.sign(
    { id: userId, username, sid: sessionId, typ: 'access' },
    config.jwtSecret,
    { expiresIn: ACCESS_EXPIRES_IN }
  );
}

function issueRefreshToken({ userId, sessionId }) {
  const tokenId = crypto.randomUUID();
  return jwt.sign(
    { id: userId, sid: sessionId, rid: tokenId, typ: 'refresh' },
    config.jwtSecret,
    { expiresIn: `${REFRESH_EXPIRES_DAYS}d` }
  );
}

function verifyToken(token) {
  return jwt.verify(token, config.jwtSecret);
}

function hashRefreshToken(token) {
  return crypto.createHash('sha256').update(String(token || '')).digest('hex');
}

function getRefreshExpiryIso() {
  const now = Date.now();
  const expiresMs = now + REFRESH_EXPIRES_DAYS * 24 * 60 * 60 * 1000;
  return new Date(expiresMs).toISOString();
}

module.exports = {
  ACCESS_EXPIRES_IN,
  REFRESH_EXPIRES_DAYS,
  issueAccessToken,
  issueRefreshToken,
  verifyToken,
  hashRefreshToken,
  getRefreshExpiryIso,
};

