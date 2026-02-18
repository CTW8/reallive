const express = require('express');
const authMiddleware = require('../middleware/auth');
const Alert = require('../models/alert');

const router = express.Router();
router.use(authMiddleware);

router.get('/', (req, res) => {
  const filters = {
    type: req.query.type,
    status: req.query.status,
    limit: req.query.limit ? parseInt(req.query.limit) : undefined,
  };
  const alerts = Alert.findByUserId(req.user.id, filters);
  res.json(alerts);
});

router.get('/stats', (req, res) => {
  const stats = Alert.getStats(req.user.id);
  res.json(stats);
});

router.get('/unread-count', (req, res) => {
  const count = Alert.getUnreadCount(req.user.id);
  res.json({ count });
});

router.get('/:id', (req, res) => {
  const alert = Alert.findById(req.params.id, req.user.id);
  if (!alert) {
    return res.status(404).json({ error: 'Alert not found' });
  }
  res.json(alert);
});

router.post('/', (req, res) => {
  const { camera_id, type, title, description, status } = req.body;
  if (!type || !title) {
    return res.status(400).json({ error: 'Type and title are required' });
  }
  const alert = Alert.create(req.user.id, { camera_id, type, title, description, status });
  res.status(201).json(alert);
});

router.patch('/:id', (req, res) => {
  const alert = Alert.findById(req.params.id, req.user.id);
  if (!alert) {
    return res.status(404).json({ error: 'Alert not found' });
  }
  const updated = Alert.update(req.params.id, req.user.id, req.body);
  res.json(updated);
});

router.post('/:id/read', (req, res) => {
  const alert = Alert.findById(req.params.id, req.user.id);
  if (!alert) {
    return res.status(404).json({ error: 'Alert not found' });
  }
  const updated = Alert.markRead(req.params.id, req.user.id);
  res.json(updated);
});

router.post('/:id/resolve', (req, res) => {
  const alert = Alert.findById(req.params.id, req.user.id);
  if (!alert) {
    return res.status(404).json({ error: 'Alert not found' });
  }
  const updated = Alert.resolve(req.params.id, req.user.id);
  res.json(updated);
});

router.post('/batch', (req, res) => {
  const { ids, action } = req.body;
  if (!ids || !Array.isArray(ids) || ids.length === 0) {
    return res.status(400).json({ error: 'ids array is required' });
  }

  switch (action) {
    case 'read':
      Alert.batchUpdate(ids, req.user.id, 'read');
      break;
    case 'resolve':
      Alert.batchUpdate(ids, req.user.id, 'resolved');
      break;
    case 'delete':
      Alert.batchDelete(ids, req.user.id);
      break;
    default:
      return res.status(400).json({ error: 'Invalid action. Use read, resolve, or delete' });
  }

  res.json({ success: true, affected: ids.length });
});

router.delete('/:id', (req, res) => {
  const alert = Alert.findById(req.params.id, req.user.id);
  if (!alert) {
    return res.status(404).json({ error: 'Alert not found' });
  }
  Alert.delete(req.params.id, req.user.id);
  res.json({ message: 'Alert deleted' });
});

module.exports = router;
