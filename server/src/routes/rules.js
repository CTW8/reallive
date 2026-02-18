const express = require('express');
const authMiddleware = require('../middleware/auth');
const Rule = require('../models/rule');

const router = express.Router();
router.use(authMiddleware);

router.get('/', (req, res) => {
  const filters = {
    priority: req.query.priority,
    enabled: req.query.enabled,
    escalation: req.query.escalation,
    query: req.query.query,
    sortBy: req.query.sortBy,
    order: req.query.order,
  };
  const rules = Rule.findByUserId(req.user.id, filters);
  res.json(rules);
});

router.get('/:id', (req, res) => {
  const rule = Rule.findById(req.params.id, req.user.id);
  if (!rule) {
    return res.status(404).json({ error: 'Rule not found' });
  }
  res.json(rule);
});

function validateRule(data, isUpdate = false) {
  const errors = [];

  if (data.name !== undefined) {
    if (data.name.length < 4 || data.name.length > 48) {
      errors.push('Rule name must be between 4 and 48 characters');
    }
  } else if (!isUpdate) {
    errors.push('Rule name is required');
  }

  if (data.condition !== undefined) {
    if (data.condition.length < 12) {
      errors.push('Condition must be at least 12 characters');
    }
  }

  if (data.actions !== undefined) {
    if (data.actions.length < 8) {
      errors.push('Actions must be at least 8 characters');
    }
  }

  if (data.priority === 'high') {
    if (data.quiet_hours && data.quiet_hours !== 'Disabled') {
      errors.push('High priority rules cannot have quiet hours enabled');
    }
    if (data.enabled === false) {
      errors.push('High priority rules cannot be disabled');
    }
    if (data.escalation === 'After 180 seconds') {
      errors.push('High priority rules cannot use 180 seconds escalation delay');
    }
  }

  return errors;
}

router.post('/', (req, res) => {
  const { name, priority, condition, actions, escalation, quiet_hours, enabled } = req.body;

  const errors = validateRule(req.body);
  if (errors.length > 0) {
    return res.status(400).json({ errors });
  }

  const existing = Rule.findByName(name, req.user.id);
  if (existing) {
    return res.status(400).json({ error: 'Rule name already exists' });
  }

  const rule = Rule.create(req.user.id, { name, priority, condition, actions, escalation, quiet_hours, enabled });
  res.status(201).json(rule);
});

router.patch('/:id', (req, res) => {
  const rule = Rule.findById(req.params.id, req.user.id);
  if (!rule) {
    return res.status(404).json({ error: 'Rule not found' });
  }

  const errors = validateRule(req.body, true);
  if (errors.length > 0) {
    return res.status(400).json({ errors });
  }

  if (req.body.name) {
    const existing = Rule.findByName(req.body.name, req.user.id, req.params.id);
    if (existing) {
      return res.status(400).json({ error: 'Rule name already exists' });
    }
  }

  const updated = Rule.update(req.params.id, req.user.id, req.body);
  res.json(updated);
});

router.post('/:id/toggle', (req, res) => {
  const rule = Rule.findById(req.params.id, req.user.id);
  if (!rule) {
    return res.status(404).json({ error: 'Rule not found' });
  }

  if (rule.priority === 'high' && rule.enabled === 1) {
    return res.status(400).json({ error: 'High priority rules cannot be disabled' });
  }

  const updated = Rule.setEnabled(req.params.id, req.user.id, !rule.enabled);
  res.json(updated);
});

router.post('/batch', (req, res) => {
  const { ids, action } = req.body;
  if (!ids || !Array.isArray(ids) || ids.length === 0) {
    return res.status(400).json({ error: 'ids array is required' });
  }

  switch (action) {
    case 'enable':
      Rule.batchUpdate(ids, req.user.id, { enabled: true });
      break;
    case 'disable':
      const rules = ids.map(id => Rule.findById(id, req.user.id)).filter(r => r);
      const hasHighPriority = rules.some(r => r.priority === 'high');
      if (hasHighPriority) {
        return res.status(400).json({ error: 'Cannot disable high priority rules' });
      }
      Rule.batchUpdate(ids, req.user.id, { enabled: false });
      break;
    case 'delete':
      Rule.batchDelete(ids, req.user.id);
      break;
    default:
      return res.status(400).json({ error: 'Invalid action. Use enable, disable, or delete' });
  }

  res.json({ success: true, affected: ids.length });
});

router.delete('/:id', (req, res) => {
  const rule = Rule.findById(req.params.id, req.user.id);
  if (!rule) {
    return res.status(404).json({ error: 'Rule not found' });
  }
  Rule.delete(req.params.id, req.user.id);
  res.json({ message: 'Rule deleted' });
});

module.exports = router;
