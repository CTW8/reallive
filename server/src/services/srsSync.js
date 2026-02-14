const Camera = require('../models/camera');
const Session = require('../models/session');
const config = require('../config');
const { startSeiMonitor, stopSeiMonitor, stopAllSeiMonitors } = require('./seiMonitor');

const SRS_API = config.srsApi || 'http://localhost:1985';
const POLL_INTERVAL = 1000; // 1 second (reduced from 5s for lower latency)
const SUMMARY_LOG_INTERVAL = 10000; // 10s summary log to avoid noisy per-poll logs
const OFFLINE_GRACE_POLLS = Math.max(1, Number(process.env.SRS_OFFLINE_GRACE_POLLS || 3));

let io = null;
let timer = null;
let lastSummaryLogAt = 0;

// Track which stream keys are currently active (to detect changes)
const activeStreams = new Set();
const missingPolls = new Map();

// Cache SRS stream info keyed by stream_key
const streamInfoCache = new Map();

// Track previous frame counts for FPS calculation
const prevFrameData = new Map();

function extractStreamKey(name) {
  if (!name) return '';
  let streamKey = String(name);
  if (streamKey.includes('/')) {
    const parts = streamKey.split('/');
    streamKey = parts[parts.length - 1];
  }
  return streamKey;
}

function isReplayStreamKey(streamKey) {
  if (!streamKey) return false;
  return /__\d+_\d+\.flv$/i.test(streamKey);
}

function estimateFps(videoInfo, prev, currentFrames, nowMs) {
  const directFps = Number(videoInfo.fps);
  if (Number.isFinite(directFps) && directFps > 0) {
    return Math.round(directFps * 10) / 10;
  }

  if (!prev || nowMs <= prev.time) {
    return prev?.fps || 0;
  }

  const timeDelta = (nowMs - prev.time) / 1000;
  const frameDelta = currentFrames - prev.frames;
  if (timeDelta < 0.5 || timeDelta > 2.0 || frameDelta < 0 || frameDelta > 120) {
    return prev.fps || 0;
  }

  let measuredFps = frameDelta / timeDelta;
  const prevFps = prev.fps || 0;
  if (prevFps > 0) {
    // Limit step changes to avoid poll jitter causing large FPS spikes.
    const maxStep = Math.max(1, prevFps * 0.12);
    measuredFps = Math.max(prevFps - maxStep, Math.min(prevFps + maxStep, measuredFps));
    measuredFps = prevFps * 0.85 + measuredFps * 0.15;
  }

  return Math.round(measuredFps * 10) / 10;
}

async function fetchSrsStreams() {
  try {
    const res = await fetch(`${SRS_API}/api/v1/streams/`);
    if (!res.ok) return null;
    const data = await res.json();
    if (data.code !== 0) return null;
    return data.streams || [];
  } catch (err) {
    console.error('[SRS Sync] Failed to fetch streams:', err.message);
    return null;
  }
}

async function syncOnce() {
  const streams = await fetchSrsStreams();
  if (!streams) return;

  // Build set of currently active stream keys from SRS
  const currentActive = new Set();
  for (const s of streams) {
    const streamKey = extractStreamKey(s.name);
    if (isReplayStreamKey(streamKey)) {
      continue;
    }

    if (s.publish && s.publish.active) {
      currentActive.add(streamKey);
      missingPolls.delete(streamKey);
      startSeiMonitor(streamKey, s.app || 'live');

      // Cache stream media info - handle both flat and nested structures
      const videoInfo = s.video || {};
      const audioInfo = s.audio || {};
      const kbpsInfo = s.kbps || {};
      
      // SRS returns kbps as object with recv_30s/send_30s or as simple number
      let kbpsData = kbpsInfo;
      if (typeof kbpsInfo === 'number') {
        kbpsData = { recv_30s: kbpsInfo, send_30s: 0 };
      }
      
      const now = Date.now();
      const currentFrames = s.frames || 0;
      const prev = prevFrameData.get(streamKey);
      const calculatedFps = estimateFps(videoInfo, prev, currentFrames, now);
      prevFrameData.set(streamKey, { frames: currentFrames, time: now, fps: calculatedFps });

      const cachedInfo = {
        codec: videoInfo.codec || 'unknown',
        profile: videoInfo.profile || '',
        level: videoInfo.level || '',
        width: videoInfo.width || 0,
        height: videoInfo.height || 0,
        fps: calculatedFps,
        audioCodec: audioInfo.codec || null,
        audioSampleRate: audioInfo.sample_rate || audioInfo.sampleRate || 0,
        kbps: kbpsData,
        clients: s.clients || 0,
        frames: currentFrames,
        recvBytes: s.recv_bytes || s.recvBytes || 0,
        publishActive: s.publish?.active || false,
        publishCid: s.publish?.cid || null,
        liveMs: s.live_ms || null,
        serverTime: now,
        lastUpdate: now,
      };
      
      streamInfoCache.set(streamKey, cachedInfo);
    }
  }

  // Detect newly started streams
  for (const streamKey of currentActive) {
    if (!activeStreams.has(streamKey)) {
      const camera = Camera.findByStreamKey(streamKey);
      if (camera && camera.status !== 'streaming') {
        Camera.updateStatus(camera.id, 'streaming');
        Session.endActiveSessionsForCamera(camera.id);
        Session.create(camera.id);

        if (io) {
          io.to(`dashboard-${camera.user_id}`).emit('camera-status', {
            cameraId: camera.id,
            status: 'streaming',
          });
          io.to(`dashboard-${camera.user_id}`).emit('activity-event', {
            type: 'stream-start',
            cameraId: camera.id,
            cameraName: camera.name,
            timestamp: new Date().toISOString(),
          });
        }
        console.log(`[SRS Sync] Camera "${camera.name}" started streaming`);
      }
      activeStreams.add(streamKey);
    }
  }

  // Detect stopped streams with debounce (avoid transient false offline)
  const keys = Array.from(activeStreams);
  for (const streamKey of keys) {
    if (!currentActive.has(streamKey)) {
      const miss = (missingPolls.get(streamKey) || 0) + 1;
      missingPolls.set(streamKey, miss);
      if (miss < OFFLINE_GRACE_POLLS) {
        continue;
      }

      stopSeiMonitor(streamKey);
      missingPolls.delete(streamKey);

      const camera = Camera.findByStreamKey(streamKey);
      if (camera && camera.status === 'streaming') {
        Camera.updateStatus(camera.id, 'offline');
        Session.endActiveSessionsForCamera(camera.id);

        if (io) {
          io.to(`dashboard-${camera.user_id}`).emit('camera-status', {
            cameraId: camera.id,
            status: 'offline',
          });
          io.to(`dashboard-${camera.user_id}`).emit('activity-event', {
            type: 'stream-stop',
            cameraId: camera.id,
            cameraName: camera.name,
            timestamp: new Date().toISOString(),
          });
        }
        console.log(`[SRS Sync] Camera "${camera.name}" stopped streaming`);
      }

      // Remove cached info
      streamInfoCache.delete(streamKey);
      prevFrameData.delete(streamKey);
      activeStreams.delete(streamKey);
    }
  }

  const now = Date.now();
  if (now - lastSummaryLogAt >= SUMMARY_LOG_INTERVAL) {
    const [sampleKey, sampleInfo] = streamInfoCache.entries().next().value || [];
    if (sampleInfo) {
      const mbps = sampleInfo.kbps?.recv_30s != null
        ? (sampleInfo.kbps.recv_30s / 1000).toFixed(1)
        : '0.0';
      console.log(
        `[SRS Sync] Active=${activeStreams.size}, sample="${sampleKey}" ` +
        `${sampleInfo.width}x${sampleInfo.height}@${sampleInfo.fps || 0}fps, ${mbps}Mbps`
      );
    } else {
      console.log(`[SRS Sync] Active=${activeStreams.size}`);
    }
    lastSummaryLogAt = now;
  }
}

function getStreamInfo(streamKey) {
  return streamInfoCache.get(streamKey) || null;
}

function startSrsSync(socketIo) {
  io = socketIo;
  syncOnce();
  timer = setInterval(syncOnce, POLL_INTERVAL);
  console.log(`[SRS Sync] Started polling SRS at ${SRS_API} every ${POLL_INTERVAL / 1000}s`);
}

function stopSrsSync() {
  if (timer) {
    clearInterval(timer);
    timer = null;
  }
  stopAllSeiMonitors();
}

module.exports = { startSrsSync, stopSrsSync, getStreamInfo };
