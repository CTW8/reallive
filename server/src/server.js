const http = require('http');
const { Server } = require('socket.io');
const app = require('./app');
const config = require('./config');
const initSignaling = require('./signaling');
const { startSrsSync } = require('./services/srsSync');

const server = http.createServer(app);

// Socket.io with CORS
const io = new Server(server, {
  cors: { origin: '*', methods: ['GET', 'POST'] },
  path: '/ws/signaling',
});

// Initialize camera status signaling
initSignaling(io);

// Start SRS stream status sync (polls SRS API to detect active streams)
startSrsSync(io);

server.listen(config.port, () => {
  console.log(`[RealLive] Server running on http://0.0.0.0:${config.port}`);
  console.log(`[RealLive] Signaling WebSocket at ws://0.0.0.0:${config.port}/ws/signaling`);
});
