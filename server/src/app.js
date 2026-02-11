const express = require('express');
const cors = require('cors');
const path = require('path');

const authRoutes = require('./routes/auth');
const cameraRoutes = require('./routes/cameras');

const app = express();

// Middleware
app.use(cors());
app.use(express.json());

// Serve Vue frontend static files
app.use(express.static(path.join(__dirname, '..', 'web', 'dist')));

// API routes
app.use('/api/auth', authRoutes);
app.use('/api/cameras', cameraRoutes);

// Health check
app.get('/api/health', (req, res) => {
  res.json({ status: 'ok', timestamp: new Date().toISOString() });
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
