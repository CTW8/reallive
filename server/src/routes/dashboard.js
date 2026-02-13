const express = require('express');
const router = express.Router();
const authMiddleware = require('../middleware/auth');
const Camera = require('../models/camera');
const Session = require('../models/session');

router.use(authMiddleware);

// GET /api/dashboard/stats
router.get('/stats', (req, res) => {
  try {
    const cameraStats = Camera.countByUserIdGroupedByStatus(req.user.id);
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
