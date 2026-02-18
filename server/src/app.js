const express = require('express');
const cors = require('cors');
const path = require('path');
const { createProxyMiddleware } = require('http-proxy-middleware');
const { RECORDINGS_ROOTS } = require('./services/historyService');

const authRoutes = require('./routes/auth');
const cameraRoutes = require('./routes/cameras');
const dashboardRoutes = require('./routes/dashboard');
const sessionRoutes = require('./routes/sessions');
const alertRoutes = require('./routes/alerts');
const ruleRoutes = require('./routes/rules');
const storageRoutes = require('./routes/storage');

const app = express();

// Middleware
app.use(cors());
app.use(express.json());

// Proxy FLV streams to SRS
// Use pathFilter instead of app.use('/live', ...) to preserve the full URL path.
// Express mount strips the prefix, causing SRS to receive /xxx.flv instead of /live/xxx.flv.
app.use(createProxyMiddleware({
  target: 'http://localhost:8080',
  pathFilter: (pathname) => pathname.startsWith('/live/') || pathname.startsWith('/history/'),
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
