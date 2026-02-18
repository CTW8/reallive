const express = require('express');
const authMiddleware = require('../middleware/auth');
const fs = require('fs');
const path = require('path');

const router = express.Router();
router.use(authMiddleware);

function getStorageInfo() {
  const recordingsPath = path.join(__dirname, '..', '..', 'recordings');
  let totalSize = 0;
  let videoSize = 0;
  let snapshotSize = 0;
  let cacheSize = 0;

  function calcDirSize(dirPath) {
    if (!fs.existsSync(dirPath)) return 0;
    let size = 0;
    const files = fs.readdirSync(dirPath);
    for (const file of files) {
      const filePath = path.join(dirPath, file);
      const stats = fs.statSync(filePath);
      if (stats.isDirectory()) {
        size += calcDirSize(filePath);
      } else {
        size += stats.size;
      }
    }
    return size;
  }

  if (fs.existsSync(recordingsPath)) {
    videoSize = calcDirSize(recordingsPath);
  }

  const snapshotsPath = path.join(__dirname, '..', '..', 'snapshots');
  if (fs.existsSync(snapshotsPath)) {
    snapshotSize = calcDirSize(snapshotsPath);
  }

  totalSize = videoSize + snapshotSize + cacheSize;

  return {
    totalSize,
    videoSize,
    snapshotSize,
    cacheSize,
  };
}

router.get('/overview', (req, res) => {
  const info = getStorageInfo();
  const totalGB = 3000;
  const usedGB = Math.round(info.totalSize / (1024 * 1024 * 1024) * 10) / 10;
  const usedPercent = Math.round((usedGB / totalGB) * 100);

  res.json({
    total: totalGB,
    used: usedGB,
    usedPercent,
    breakdown: {
      video: Math.round(info.videoSize / (1024 * 1024 * 1024) * 10) / 10,
      snapshots: Math.round(info.snapshotSize / (1024 * 1024 * 1024) * 10) / 10,
      cache: Math.round(info.cacheSize / (1024 * 1024 * 1024) * 10) / 10,
    },
  });
});

router.get('/trend', (req, res) => {
  const days = parseInt(req.query.days) || 14;
  const trend = [];
  let baseUsage = 60;

  for (let i = days - 1; i >= 0; i--) {
    const date = new Date();
    date.setDate(date.getDate() - i);
    const usage = baseUsage + Math.round(Math.random() * 10);
    baseUsage = usage;
    trend.push({
      date: date.toISOString().split('T')[0],
      usage: usage,
    });
  }

  res.json({ trend });
});

router.get('/by-device', (req, res) => {
  const db = require('../models/db');
  const cameras = db.prepare('SELECT id, name FROM cameras WHERE user_id = ?').all(req.user.id);

  const devices = cameras.map(cam => ({
    id: cam.id,
    name: cam.name,
    videos: Math.round(Math.random() * 500),
    snapshots: Math.round(Math.random() * 200),
    usage: Math.round(Math.random() * 60),
  }));

  res.json({ devices });
});

router.post('/policy', (req, res) => {
  const { retention_days, cloud_backup, snapshot_retention, cache_limit } = req.body;

  res.json({
    success: true,
    policy: {
      retention_days: retention_days || 14,
      cloud_backup: cloud_backup || false,
      snapshot_retention: snapshot_retention || 7,
      cache_limit: cache_limit || 2,
    },
  });
});

module.exports = router;
