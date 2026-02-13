<script setup>
import { ref, onMounted, onBeforeUnmount, nextTick } from 'vue'
import { useCameraStore } from '../stores/camera.js'
import { useAuthStore } from '../stores/auth.js'
import { connectSignaling } from '../api/signaling.js'
import mpegts from 'mpegts.js'

const cameraStore = useCameraStore()
const auth = useAuthStore()

const videoRefs = ref({})
const connectionStates = ref({})
const columns = ref(2)

let socket = null
const players = {}

onMounted(async () => {
  await cameraStore.fetchCameras()

  socket = connectSignaling(auth.token)
  socket.on('camera-status', ({ cameraId, status }) => {
    if (status === 'offline' && players[cameraId]) {
      stopStream(cameraId)
      connectionStates.value[cameraId] = 'ended'
    }
  })

  await nextTick()

  for (const camera of cameraStore.cameras) {
    if (camera.status !== 'offline') {
      startStream(camera)
    }
  }
})

onBeforeUnmount(() => {
  for (const id of Object.keys(players)) {
    stopStream(id)
  }
})

function setVideoRef(el, id) {
  if (el) {
    videoRefs.value[id] = el
  }
}

function startStream(camera) {
  const id = camera.id
  connectionStates.value[id] = 'connecting'

  if (!mpegts.isSupported()) {
    connectionStates.value[id] = 'error'
    return
  }

  const flvUrl = camera.flvUrl || `${window.location.origin}/live/${camera.stream_key}.flv`

  const player = mpegts.createPlayer({
    type: 'flv',
    isLive: true,
    url: flvUrl,
  }, {
    enableWorker: true,
    enableStashBuffer: false,
    stashInitialSize: 128,
    lazyLoad: false,
    liveBufferLatencyChasing: true,
    liveBufferLatencyMaxLatency: 0.8,
    liveBufferLatencyMinRemain: 0.2,
    liveBufferLatencyChasingOnPaused: true,
  })

  players[id] = player

  const videoEl = videoRefs.value[id]
  if (!videoEl) {
    connectionStates.value[id] = 'error'
    return
  }

  player.attachMediaElement(videoEl)

  player.on(mpegts.Events.ERROR, () => {
    connectionStates.value[id] = 'error'
  })

  player.on(mpegts.Events.LOADING_COMPLETE, () => {
    connectionStates.value[id] = 'ended'
  })

  player.on(mpegts.Events.MEDIA_INFO, () => {
    connectionStates.value[id] = 'connected'
  })

  player.load()
  player.play()
}

function stopStream(id) {
  const player = players[id]
  if (player) {
    player.pause()
    player.unload()
    player.detachMediaElement()
    player.destroy()
    delete players[id]
  }
  delete connectionStates.value[id]
}

function stateLabel(id) {
  return connectionStates.value[id] || 'offline'
}

function stateClass(id) {
  const state = connectionStates.value[id] || 'offline'
  return `status-badge status-${state}`
}
</script>

<template>
  <div class="multiview">
    <div class="multiview-header">
      <div>
        <h1>Multi View</h1>
        <p class="subtitle">Viewing all active camera streams</p>
      </div>
      <div class="column-selector">
        <label>Grid</label>
        <select v-model="columns">
          <option :value="1">1 Column</option>
          <option :value="2">2 Columns</option>
          <option :value="3">3 Columns</option>
          <option :value="4">4 Columns</option>
        </select>
      </div>
    </div>

    <div v-if="cameraStore.cameras.length === 0" class="empty-state">
      <p>No cameras configured.</p>
    </div>

    <div
      v-else
      class="stream-grid"
      :style="{ gridTemplateColumns: `repeat(${columns}, 1fr)` }"
    >
      <div v-for="camera in cameraStore.cameras" :key="camera.id" class="stream-cell">
        <div class="cell-header">
          <span class="cell-name">{{ camera.name }}</span>
          <span :class="stateClass(camera.id)">{{ stateLabel(camera.id) }}</span>
        </div>
        <div class="cell-video">
          <video
            :ref="(el) => setVideoRef(el, camera.id)"
            autoplay
            playsinline
            muted
          ></video>
          <div v-if="camera.status === 'offline'" class="cell-overlay">
            <span>Offline</span>
          </div>
          <div v-else-if="stateLabel(camera.id) === 'connecting'" class="cell-overlay">
            <span>Connecting...</span>
          </div>
          <div v-else-if="stateLabel(camera.id) === 'error'" class="cell-overlay">
            <span>Error</span>
          </div>
        </div>
      </div>
    </div>
  </div>
</template>

<style scoped>
.multiview-header {
  display: flex;
  justify-content: space-between;
  align-items: flex-start;
  margin-bottom: 24px;
}

.multiview-header h1 {
  font-size: 1.5rem;
  font-weight: 700;
}

.subtitle {
  color: var(--text-secondary);
  font-size: 0.85rem;
  margin-top: 2px;
}

.column-selector {
  display: flex;
  align-items: center;
  gap: 8px;
}

.column-selector label {
  font-size: 0.85rem;
  color: var(--text-secondary);
}

.column-selector select {
  padding: 6px 12px;
  background: var(--bg-input);
  border: 1px solid var(--border-color);
  border-radius: var(--radius);
  color: var(--text-primary);
  font-size: 0.85rem;
}

.stream-grid {
  display: grid;
  gap: 12px;
}

.stream-cell {
  background: var(--bg-card);
  border: 1px solid var(--border-color);
  border-radius: var(--radius-lg);
  overflow: hidden;
}

.cell-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  padding: 10px 14px;
  border-bottom: 1px solid var(--border-color);
}

.cell-name {
  font-size: 0.88rem;
  font-weight: 600;
}

.cell-video {
  position: relative;
  aspect-ratio: 16 / 9;
  background: #000;
}

.cell-video video {
  width: 100%;
  height: 100%;
  object-fit: contain;
  display: block;
}

.cell-overlay {
  position: absolute;
  inset: 0;
  display: flex;
  align-items: center;
  justify-content: center;
  background: rgba(0, 0, 0, 0.6);
  color: var(--text-secondary);
  font-size: 0.85rem;
}

.empty-state {
  text-align: center;
  padding: 60px 20px;
  color: var(--text-secondary);
}

.status-connecting {
  background: rgba(251, 191, 36, 0.15);
  color: var(--warning);
}

.status-connected {
  background: rgba(52, 211, 153, 0.15);
  color: var(--success);
}

.status-ended,
.status-error {
  background: rgba(248, 113, 113, 0.15);
  color: var(--danger);
}
</style>
