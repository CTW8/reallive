const express = require('express');
const cors = require('cors');
const path = require('path');
const crypto = require('crypto');
const { createProxyMiddleware } = require('http-proxy-middleware');
const { RECORDINGS_ROOTS } = require('./services/historyService');
const config = require('./config');

const authRoutes = require('./routes/auth');
const cameraRoutes = require('./routes/cameras');
const dashboardRoutes = require('./routes/dashboard');
const sessionRoutes = require('./routes/sessions');
const alertRoutes = require('./routes/alerts');
const ruleRoutes = require('./routes/rules');
const storageRoutes = require('./routes/storage');
const settingsRoutes = require('./routes/settings');

const app = express();

// Middleware
app.use(cors());
app.use(express.json());

function timingSafeEqualString(a, b) {
  const aa = Buffer.from(String(a || ''));
  const bb = Buffer.from(String(b || ''));
  if (aa.length !== bb.length) return false;
  return crypto.timingSafeEqual(aa, bb);
}

const consumedShareTokenByJti = new Map();

function cleanupConsumedShareTokens(nowMs = Date.now()) {
  if (consumedShareTokenByJti.size <= 0) return;
  for (const [jti, expiresAt] of consumedShareTokenByJti.entries()) {
    if (!expiresAt || expiresAt <= nowMs) consumedShareTokenByJti.delete(jti);
  }
}

function verifyShareToken(st, expectedStreamKey) {
  if (!st || typeof st !== 'string') return { ok: false, code: 400, error: 'Missing share token' };
  const parts = st.split('.');
  if (parts.length !== 2 || !parts[0] || !parts[1]) {
    return { ok: false, code: 400, error: 'Invalid share token format' };
  }

  const [body, sig] = parts;
  const expectedSig = crypto
    .createHmac('sha256', config.jwtSecret || 'reallive-share')
    .update(body)
    .digest('base64url');

  if (!timingSafeEqualString(sig, expectedSig)) {
    return { ok: false, code: 403, error: 'Invalid share token signature' };
  }

  let payload;
  try {
    payload = JSON.parse(Buffer.from(body, 'base64url').toString('utf8'));
  } catch (_err) {
    return { ok: false, code: 400, error: 'Malformed share token payload' };
  }

  const exp = Number(payload?.exp || 0);
  const tokenStreamKey = String(payload?.stream_key || '');
  const oneTime = !!payload?.one_time;
  const jti = String(payload?.jti || '');
  if (!exp || exp <= Date.now()) {
    return { ok: false, code: 403, error: 'Share link expired' };
  }
  if (!tokenStreamKey || tokenStreamKey !== expectedStreamKey) {
    return { ok: false, code: 403, error: 'Share token stream mismatch' };
  }
  if (oneTime) {
    if (!jti) return { ok: false, code: 403, error: 'Invalid one-time token' };
    cleanupConsumedShareTokens(Date.now());
    if (consumedShareTokenByJti.has(jti)) {
      return { ok: false, code: 403, error: 'One-time share token already used' };
    }
    consumedShareTokenByJti.set(jti, exp);
  }

  return { ok: true, payload };
}

// Proxy FLV streams to SRS
// Use pathFilter instead of app.use('/live', ...) to preserve the full URL path.
// Express mount strips the prefix, causing SRS to receive /xxx.flv instead of /live/xxx.flv.
app.use('/share/live', (req, res, next) => {
  const rawPath = String(req.path || '');
  const streamWithExt = rawPath.replace(/^\/+/, '').split('/')[0] || '';
  const streamKey = streamWithExt.replace(/\.(flv|m3u8)$/i, '');
  if (!streamKey) {
    return res.status(400).json({ error: 'Missing stream key' });
  }
  const st = String(req.query?.st || '');
  const verified = verifyShareToken(st, streamKey);
  if (!verified.ok) {
    return res.status(verified.code || 403).json({ error: verified.error || 'Forbidden' });
  }
  return next();
});

app.use(createProxyMiddleware({
  target: 'http://localhost:8080',
  pathFilter: (pathname) =>
    pathname.startsWith('/live/') ||
    pathname.startsWith('/history/') ||
    pathname.startsWith('/share/live/'),
  pathRewrite: (path) => (path.startsWith('/share/live/') ? path.replace('/share/live/', '/live/') : path),
  changeOrigin: true,
  // Disable proxy timeout for long-lived HTTP-FLV streaming connections
  timeout: 0,
  proxyTimeout: 0,
}));

// Serve Vue frontend static files
app.use(express.static(path.join(__dirname, '..', 'web', 'dist')));
RECORDINGS_ROOTS.forEach((root, idx) => {
  app.use(`/history-files/${idx}`, express.static(root));
});

// API routes
app.use('/api/auth', authRoutes);
app.use('/api/cameras', cameraRoutes);
app.use('/api/dashboard', dashboardRoutes);
app.use('/api/sessions', sessionRoutes);
app.use('/api/alerts', alertRoutes);
app.use('/api/alert-rules', ruleRoutes);
app.use('/api/storage', storageRoutes);
app.use('/api/settings', settingsRoutes);

// Health check
app.get('/api/health', (req, res) => {
  res.json({
    status: 'ok',
    timestamp: new Date().toISOString(),
    uptime: Math.floor(process.uptime()),
    memoryUsage: process.memoryUsage().rss,
    nodeVersion: process.version,
  });
});

// SPA fallback - serve index.html for non-API routes
app.get('*', (req, res) => {
  const indexPath = path.join(__dirname, '..', 'web', 'dist', 'index.html');
  res.sendFile(indexPath, (err) => {
    if (err) {
      res.status(404).json({ error: 'Not found' });
    }
  });
});

module.exports = app;
