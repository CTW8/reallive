const express = require('express');
const router = express.Router();
const authMiddleware = require('../middleware/auth');
const Session = require('../models/session');

router.use(authMiddleware);

// GET /api/sessions?limit=20&offset=0
router.get('/', (req, res) => {
  try {
    const limit = Math.min(parseInt(req.query.limit) || 20, 100);
    const offset = parseInt(req.query.offset) || 0;
    const result = Session.findByUserId(req.user.id, limit, offset);

    const sessions = result.sessions.map((s) => ({
      ...s,
      duration_seconds: s.end_time
        ? Math.floor((new Date(s.end_time) - new Date(s.start_time)) / 1000)
        : Math.floor((Date.now() - new Date(s.start_time).getTime()) / 1000),
    }));

    res.json({ sessions, total: result.total });
  } catch (err) {
    res.status(500).json({ error: 'Failed to fetch sessions' });
  }
});

// GET /api/sessions/active
router.get('/active', (req, res) => {
  try {
    const result = Session.findByUserId(req.user.id, 100, 0);
    const active = result.sessions
      .filter((s) => s.status === 'active')
      .map((s) => ({
        ...s,
        duration_seconds: Math.floor(
          (Date.now() - new Date(s.start_time).getTime()) / 1000
        ),
      }));

    res.json({ sessions: active });
  } catch (err) {
    res.status(500).json({ error: 'Failed to fetch active sessions' });
  }
});

module.exports = router;
