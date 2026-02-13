const jwt = require('jsonwebtoken');
const config = require('../config');
const Camera = require('../models/camera');
const Session = require('../models/session');

function initSignaling(io) {
  // Authenticate socket connections via JWT
  io.use((socket, next) => {
    const token = socket.handshake.auth.token;
    if (!token) {
      return next(new Error('Authentication required'));
    }
    try {
      const payload = jwt.verify(token, config.jwtSecret);
      socket.user = { id: payload.id, username: payload.username };
      next();
    } catch (err) {
      next(new Error('Invalid token'));
    }
  });

  io.on('connection', (socket) => {
    console.log(`[signaling] User ${socket.user.username} connected (${socket.id})`);

    // Join a room for status updates (camera room or dashboard room)
    socket.on('join-room', (data) => {
      const { room } = data;
      if (!room) return;
      socket.join(room);
      console.log(`[signaling] ${socket.user.username} joined room ${room}`);
    });

    // Camera stream start - update status and create session
    socket.on('stream-start', (data) => {
      const { streamKey } = data;
      const camera = Camera.findByStreamKey(streamKey);
      if (camera && camera.user_id === socket.user.id) {
        Camera.updateStatus(camera.id, 'streaming');

        // Close any stale sessions then create a new one
        Session.endActiveSessionsForCamera(camera.id);
        Session.create(camera.id);

        const room = `camera-${camera.id}`;
        socket.join(room);
        io.to(room).emit('camera-status', {
          cameraId: camera.id,
          status: 'streaming',
        });

        // Broadcast activity event to dashboard
        io.to(`dashboard-${camera.user_id}`).emit('activity-event', {
          type: 'stream-start',
          cameraId: camera.id,
          cameraName: camera.name,
          timestamp: new Date().toISOString(),
        });

        console.log(`[signaling] Camera ${camera.name} started streaming`);
      }
    });

    // Camera stream stop - update status and close session
    socket.on('stream-stop', (data) => {
      const { streamKey } = data;
      const camera = Camera.findByStreamKey(streamKey);
      if (camera && camera.user_id === socket.user.id) {
        Camera.updateStatus(camera.id, 'offline');
        Session.endActiveSessionsForCamera(camera.id);

        const room = `camera-${camera.id}`;
        io.to(room).emit('camera-status', {
          cameraId: camera.id,
          status: 'offline',
        });

        // Broadcast activity event to dashboard
        io.to(`dashboard-${camera.user_id}`).emit('activity-event', {
          type: 'stream-stop',
          cameraId: camera.id,
          cameraName: camera.name,
          timestamp: new Date().toISOString(),
        });

        console.log(`[signaling] Camera ${camera.name} stopped streaming`);
      }
    });

    socket.on('disconnect', () => {
      console.log(`[signaling] User ${socket.user.username} disconnected`);
    });
  });
}

module.exports = initSignaling;
