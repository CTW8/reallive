const Camera = require('../models/camera');
const Session = require('../models/session');

const SRS_API = process.env.SRS_API || 'http://localhost:1985';
const POLL_INTERVAL = 5000; // 5 seconds

let io = null;
let timer = null;

// Track which stream keys are currently active (to detect changes)
const activeStreams = new Set();

// Cache SRS stream info keyed by stream_key
const streamInfoCache = new Map();

// Track previous frame counts for FPS calculation
const prevFrameData = new Map();

async function fetchSrsStreams() {
  try {
    const res = await fetch(`${SRS_API}/api/v1/streams/`);
    if (!res.ok) return null;
    const data = await res.json();
    if (data.code !== 0) return null;
    console.log('[SRS Sync] Fetched', (data.streams || []).length, 'streams from SRS');
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
    // SRS stream name may contain app prefix (e.g., "live/stream_key"), extract the last part
    let streamKey = s.name;
    if (streamKey.includes('/')) {
      const parts = streamKey.split('/');
      streamKey = parts[parts.length - 1];
    }
    
    if (s.publish && s.publish.active) {
      currentActive.add(streamKey);

      // Cache stream media info - handle both flat and nested structures
      const videoInfo = s.video || {};
      const audioInfo = s.audio || {};
      const kbpsInfo = s.kbps || {};
      
      // SRS returns kbps as object with recv_30s/send_30s or as simple number
      let kbpsData = kbpsInfo;
      if (typeof kbpsInfo === 'number') {
        kbpsData = { recv_30s: kbpsInfo, send_30s: 0 };
      }
      
      // Calculate FPS from frame count delta between polls
      const now = Date.now();
      const currentFrames = s.frames || 0;
      const prev = prevFrameData.get(streamKey);
      let calculatedFps = 0;
      if (prev && now > prev.time) {
        const timeDelta = (now - prev.time) / 1000;
        const frameDelta = currentFrames - prev.frames;
        if (timeDelta > 0 && frameDelta >= 0) {
          calculatedFps = Math.round(frameDelta / timeDelta);
        }
      }
      prevFrameData.set(streamKey, { frames: currentFrames, time: now });

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
        lastUpdate: now,
      };
      
      streamInfoCache.set(streamKey, cachedInfo);
      console.log(`[SRS Sync] Cached stream info for "${streamKey}":`, 
        `${cachedInfo.width}x${cachedInfo.height}@${cachedInfo.fps}fps, ` +
        `${(cachedInfo.kbps.recv_30s / 1000).toFixed(1)}Mbps`);
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
    }
  }

  // Detect stopped streams
  for (const streamKey of activeStreams) {
    if (!currentActive.has(streamKey)) {
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
    }
  }

  // Update tracking set
  activeStreams.clear();
  for (const key of currentActive) {
    activeStreams.add(key);
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
}

module.exports = { startSrsSync, stopSrsSync, getStreamInfo };
