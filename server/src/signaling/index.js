const jwt = require('jsonwebtoken');
const config = require('../config');
const Camera = require('../models/camera');

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

    // Join a camera's signaling room
    socket.on('join-room', (data) => {
      const { room } = data;
      if (!room) return;
      socket.join(room);
      console.log(`[signaling] ${socket.user.username} joined room ${room}`);
      // Notify others in the room
      socket.to(room).emit('peer-joined', {
        peerId: socket.id,
        username: socket.user.username,
      });
    });

    // WebRTC offer
    socket.on('offer', (data) => {
      const { room, offer } = data;
      socket.to(room).emit('offer', {
        peerId: socket.id,
        offer,
      });
    });

    // WebRTC answer
    socket.on('answer', (data) => {
      const { room, answer } = data;
      socket.to(room).emit('answer', {
        peerId: socket.id,
        answer,
      });
    });

    // ICE candidate
    socket.on('ice-candidate', (data) => {
      const { room, candidate } = data;
      socket.to(room).emit('ice-candidate', {
        peerId: socket.id,
        candidate,
      });
    });

    // Camera stream start - update camera status
    socket.on('stream-start', (data) => {
      const { streamKey } = data;
      const camera = Camera.findByStreamKey(streamKey);
      if (camera && camera.user_id === socket.user.id) {
        Camera.updateStatus(camera.id, 'streaming');
        const room = `camera-${camera.id}`;
        socket.join(room);
        io.to(room).emit('camera-status', {
          cameraId: camera.id,
          status: 'streaming',
        });
        console.log(`[signaling] Camera ${camera.name} started streaming`);
      }
    });

    // Camera stream stop
    socket.on('stream-stop', (data) => {
      const { streamKey } = data;
      const camera = Camera.findByStreamKey(streamKey);
      if (camera && camera.user_id === socket.user.id) {
        Camera.updateStatus(camera.id, 'offline');
        const room = `camera-${camera.id}`;
        io.to(room).emit('camera-status', {
          cameraId: camera.id,
          status: 'offline',
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
