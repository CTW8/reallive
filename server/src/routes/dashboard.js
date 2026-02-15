const express = require('express');
const router = express.Router();
const authMiddleware = require('../middleware/auth');
const Camera = require('../models/camera');
const Session = require('../models/session');
const { getDeviceState } = require('../services/mqttControlService');

router.use(authMiddleware);

// GET /api/dashboard/stats
router.get('/stats', (req, res) => {
  try {
    const cameras = Camera.findByUserId(req.user.id);
    const cameraStats = {
      total: cameras.length,
      online: 0,
      streaming: 0,
      offline: 0,
    };
    for (const camera of cameras) {
      const device = getDeviceState(camera.stream_key);
      const status = device
        ? (device.activeLive ? 'streaming' : 'online')
        : (camera.status || 'offline');
      if (status === 'streaming') cameraStats.streaming += 1;
      else if (status === 'online') cameraStats.online += 1;
      else cameraStats.offline += 1;
    }
    const sessionStats = Session.getStatsForUser(req.user.id);

    res.json({
      cameras: cameraStats,
      sessions: sessionStats,
      system: {
        uptime: Math.floor(process.uptime()),
        timestamp: new Date().toISOString(),
      },
    });
  } catch (err) {
    res.status(500).json({ error: 'Failed to fetch dashboard stats' });
  }
});

module.exports = router;
