import { io } from 'socket.io-client'

let socket = null

export function connectSignaling(token) {
  if (socket) return socket

  socket = io('/', {
    path: '/ws/signaling',
    auth: { token },
    transports: ['websocket'],
  })

  socket.on('connect', () => {
    console.log('[Signaling] Connected:', socket.id)
  })

  socket.on('disconnect', (reason) => {
    console.log('[Signaling] Disconnected:', reason)
  })

  socket.on('connect_error', (err) => {
    console.error('[Signaling] Connection error:', err.message)
  })

  return socket
}

export function getSocket() {
  return socket
}

export function disconnectSignaling() {
  if (socket) {
    socket.disconnect()
    socket = null
  }
}
