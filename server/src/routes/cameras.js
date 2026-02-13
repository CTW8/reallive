const express = require('express');
const { v4: uuidv4 } = require('uuid');
const authMiddleware = require('../middleware/auth');
const Camera = require('../models/camera');
const { getStreamInfo } = require('../services/srsSync');

const router = express.Router();

// All camera routes require authentication
router.use(authMiddleware);

// GET /api/cameras
router.get('/', (req, res) => {
  const cameras = Camera.findByUserId(req.user.id);
  res.json(cameras);
});

// POST /api/cameras
router.post('/', (req, res) => {
  const { name, resolution } = req.body;
  if (!name) {
    return res.status(400).json({ error: 'Camera name is required' });
  }

  const streamKey = uuidv4();
  const camera = Camera.create(req.user.id, name, streamKey, resolution);
  res.status(201).json(camera);
});

// PUT /api/cameras/:id
router.put('/:id', (req, res) => {
  const camera = Camera.findById(req.params.id);
  if (!camera) {
    return res.status(404).json({ error: 'Camera not found' });
  }
  if (camera.user_id !== req.user.id) {
    return res.status(403).json({ error: 'Forbidden' });
  }

  const updated = Camera.update(req.params.id, req.user.id, req.body);
  res.json(updated);
});

// DELETE /api/cameras/:id
router.delete('/:id', (req, res) => {
  const camera = Camera.findById(req.params.id);
  if (!camera) {
    return res.status(404).json({ error: 'Camera not found' });
  }
  if (camera.user_id !== req.user.id) {
    return res.status(403).json({ error: 'Forbidden' });
  }

  Camera.delete(req.params.id, req.user.id);
  res.json({ message: 'Camera deleted' });
});

// GET /api/cameras/:id/stream
router.get('/:id/stream', (req, res) => {
  const camera = Camera.findById(req.params.id);
  if (!camera) {
    console.log(`[Camera API] Camera not found: ${req.params.id}`);
    return res.status(404).json({ error: 'Camera not found' });
  }
  if (camera.user_id !== req.user.id) {
    return res.status(403).json({ error: 'Forbidden' });
  }

  // Get real-time SRS stream info if available
  const srsInfo = getStreamInfo(camera.stream_key);
  console.log(`[Camera API] Getting stream info for camera ${camera.id}, stream_key=${camera.stream_key}, srs=${JSON.stringify(srsInfo)}`);

  res.json({
    camera: {
      id: camera.id,
      name: camera.name,
      resolution: camera.resolution,
      status: camera.status,
    },
    stream_key: camera.stream_key,
    signaling_url: `/ws/signaling`,
    room: `camera-${camera.id}`,
    status: camera.status,
    srs: srsInfo,
  });
});

module.exports = router;
