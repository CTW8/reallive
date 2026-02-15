const http = require('http');
const { Server } = require('socket.io');
const app = require('./app');
const config = require('./config');
const initSignaling = require('./signaling');
const { startSrsSync } = require('./services/srsSync');
const { setSeiEventEmitter } = require('./services/seiMonitor');
const { getLatestThumbnail } = require('./services/historyService');
const liveDemandService = require('./services/liveDemandService');
const { start: startMqttControl, setStateEventEmitter } = require('./services/mqttControlService');
const Camera = require('./models/camera');

const server = http.createServer(app);

// Socket.io with CORS
const io = new Server(server, {
  cors: { origin: '*', methods: ['GET', 'POST'] },
  path: '/ws/signaling',
});

// Initialize camera status signaling
initSignaling(io);

setSeiEventEmitter((streamKey, event) => {
  const camera = Camera.findByStreamKey(streamKey);
  if (!camera) return;
  io.to(`dashboard-${camera.user_id}`).emit('activity-event', {
    type: event.type,
    cameraId: camera.id,
    cameraName: camera.name,
    timestamp: new Date(event.ts || Date.now()).toISOString(),
    score: event.score,
    bbox: event.bbox || null,
  });
});

setStateEventEmitter((streamKey, payload) => {
  const camera = Camera.findByStreamKey(streamKey);
  if (!camera) return;

  const status = String(payload?.status || 'offline');
  const prevStatus = String(camera.status || 'offline');
  if (status !== prevStatus) {
    Camera.updateStatus(camera.id, status);
  }

  const thumbnailUrl = status === 'offline' ? null : getLatestThumbnail(streamKey);
  io.to(`dashboard-${camera.user_id}`).emit('camera-status', {
    cameraId: camera.id,
    status,
    thumbnailUrl,
    runtime: payload?.runtime || null,
  });
});

// Start SRS stream status sync (polls SRS API to detect active streams)
startSrsSync(io);
startMqttControl();
liveDemandService.start();

server.listen(config.port, () => {
  console.log(`[RealLive] Server running on http://0.0.0.0:${config.port}`);
  console.log(`[RealLive] Signaling WebSocket at ws://0.0.0.0:${config.port}/ws/signaling`);
});
