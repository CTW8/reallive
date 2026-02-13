<script setup>
import { ref, onMounted, onBeforeUnmount } from 'vue'
import { useRoute, useRouter } from 'vue-router'
import { useAuthStore } from '../stores/auth.js'
import { cameraApi } from '../api/index.js'
import { connectSignaling } from '../api/signaling.js'
import mpegts from 'mpegts.js'

const route = useRoute()
const router = useRouter()
const auth = useAuthStore()

const cameraId = route.params.id
const streamInfo = ref(null)
const error = ref('')
const connectionState = ref('connecting')

const videoRef = ref(null)
let player = null
let socket = null

let refreshTimer = null
let latencyTimer = null

// Latency metrics
const latencyInfo = ref({
  buffer: 0,       // video buffer ahead (seconds)
  e2e: null,       // end-to-end latency estimate (seconds)
  dropped: 0,      // dropped frames
  decoded: 0,      // decoded frames
  speed: 0,        // download speed KB/s
})

// Server time offset for e2e latency calculation
let serverTimeOffset = 0
let streamStartServerTime = 0

onMounted(async () => {
  try {
    // Measure server time offset for e2e latency
    await calibrateServerTime()
    await loadStreamInfo()
    listenCameraStatus()
    refreshTimer = setInterval(loadStreamInfo, 3000)
    // Update latency metrics every 200ms
    latencyTimer = setInterval(updateLatencyMetrics, 200)
  } catch (err) {
    error.value = err.message || 'Failed to load stream info'
    connectionState.value = 'error'
  }
})

async function calibrateServerTime() {
  try {
    const t0 = Date.now()
    const res = await fetch('/api/health')
    const data = await res.json()
    const t1 = Date.now()
    const rtt = t1 - t0
    const serverTime = new Date(data.timestamp).getTime()
    // Server time at midpoint of request
    serverTimeOffset = serverTime - (t0 + rtt / 2)
  } catch {
    serverTimeOffset = 0
  }
}

function updateLatencyMetrics() {
  const video = videoRef.value
  if (!video || !player) return

  const info = { ...latencyInfo.value }

  // Buffer latency: how much data is buffered ahead of playback
  if (video.buffered.length > 0) {
    const bufferEnd = video.buffered.end(video.buffered.length - 1)
    info.buffer = Math.max(0, bufferEnd - video.currentTime)
  }

  // End-to-end latency estimate:
  // SRS lastUpdate is when the server last saw the stream data.
  // Compare with current browser time (adjusted for clock offset).
  if (streamInfo.value?.srs?.lastUpdate) {
    const serverNow = Date.now() + serverTimeOffset
    const srsAge = (serverNow - streamInfo.value.srs.lastUpdate) / 1000
    // e2e = SRS polling delay + buffer latency + SRS processing
    // srsAge is how old the SRS data is (polling interval ~5s, so not great)
    // Better: use video.currentTime relative to stream start
    info.e2e = info.buffer + srsAge
  }

  // Dropped/decoded frames from browser
  if (video.getVideoPlaybackQuality) {
    const quality = video.getVideoPlaybackQuality()
    info.dropped = quality.droppedVideoFrames || 0
    info.decoded = quality.totalVideoFrames || 0
  }

  // mpegts.js statistics
  if (player.statisticsInfo) {
    info.speed = Math.round(player.statisticsInfo.speed || 0)
  }

  latencyInfo.value = info
}

async function loadStreamInfo() {
  try {
    const data = await cameraApi.getStreamInfo(cameraId)
    streamInfo.value = data
    if (!player && data.stream_key) {
      const flvUrl = `${window.location.origin}/live/${data.stream_key}.flv`
      startFlvPlayer(flvUrl)
    }
  } catch (err) {
    console.error('Failed to load stream info:', err)
  }
}

onBeforeUnmount(() => {
  cleanup()
})

function cleanup() {
  if (player) {
    player.pause()
    player.unload()
    player.detachMediaElement()
    player.destroy()
    player = null
  }
  if (socket) {
    socket.off('camera-status')
  }
  if (refreshTimer) {
    clearInterval(refreshTimer)
    refreshTimer = null
  }
  if (latencyTimer) {
    clearInterval(latencyTimer)
    latencyTimer = null
  }
}

function startFlvPlayer(url) {
  if (!mpegts.isSupported()) {
    error.value = 'Your browser does not support HTTP-FLV playback'
    connectionState.value = 'error'
    return
  }

  player = mpegts.createPlayer({
    type: 'flv',
    isLive: true,
    url: url,
  }, {
    enableWorker: true,
    enableStashBuffer: false,
    stashInitialSize: 128,
    lazyLoad: false,
    lazyLoadMaxDuration: 0.2,
    deferLoadAfterSourceOpen: false,
    liveBufferLatencyChasing: true,
    liveBufferLatencyMaxLatency: 0.8,
    liveBufferLatencyMinRemain: 0.2,
    liveBufferLatencyChasingOnPaused: true,
  })

  player.attachMediaElement(videoRef.value)

  player.on(mpegts.Events.ERROR, (errorType, detail) => {
    console.error('[FLV Player] Error:', errorType, detail)
    error.value = `Playback error: ${detail}`
    connectionState.value = 'error'
  })

  player.on(mpegts.Events.LOADING_COMPLETE, () => {
    connectionState.value = 'ended'
  })

  player.on(mpegts.Events.MEDIA_INFO, () => {
    connectionState.value = 'connected'
  })

  player.on(mpegts.Events.STATISTICS_INFO, (stats) => {
    if (stats.speed !== undefined) {
      latencyInfo.value.speed = Math.round(stats.speed)
    }
  })

  player.load()
  player.play()
  connectionState.value = 'connecting'
}

function listenCameraStatus() {
  socket = connectSignaling(auth.token)
  socket.emit('join-room', { room: `camera-${cameraId}` })

  socket.on('camera-status', ({ cameraId: id, status }) => {
    if (id == cameraId && status === 'offline') {
      connectionState.value = 'ended'
    }
  })
}

function goBack() {
  router.push('/')
}

function latencyClass(val) {
  if (val < 0.5) return 'latency-good'
  if (val < 1.5) return 'latency-warn'
  return 'latency-bad'
}
</script>

<template>
  <div class="watch-view">
    <div class="watch-header">
      <button class="btn btn-secondary btn-sm" @click="goBack">
        &larr; Back
      </button>
      <div class="watch-info">
        <h2>{{ streamInfo?.camera?.name || 'Camera Stream' }}</h2>
        <span :class="'status-badge status-' + connectionState">{{ connectionState }}</span>
      </div>
    </div>

    <div class="video-container">
      <video
        ref="videoRef"
        autoplay
        playsinline
        muted
        class="video-player"
      ></video>
      <div v-if="connectionState === 'connecting'" class="video-overlay">
        <p>Connecting to stream...</p>
      </div>
      <div v-if="connectionState === 'error'" class="video-overlay video-overlay-error">
        <p>{{ error || 'Connection failed' }}</p>
        <button class="btn btn-primary btn-sm" @click="goBack">Return to Dashboard</button>
      </div>
      <div v-if="connectionState === 'ended'" class="video-overlay">
        <p>Stream has ended</p>
        <button class="btn btn-primary btn-sm" @click="goBack">Return to Dashboard</button>
      </div>

      <!-- 底部推流信息栏 -->
      <div v-if="streamInfo?.srs && connectionState === 'connected'" class="stream-bar">
        <!-- 延迟指标 (高亮显示) -->
        <span class="bar-item bar-latency" :class="latencyClass(latencyInfo.buffer)">
          Buffer: {{ latencyInfo.buffer.toFixed(2) }}s
        </span>
        <span class="bar-sep"></span>
        <span class="bar-item">
          {{ streamInfo.srs.codec || '?' }}
          {{ streamInfo.srs.profile ? `(${streamInfo.srs.profile})` : '' }}
        </span>
        <span class="bar-sep"></span>
        <span class="bar-item">
          {{ streamInfo.srs.width && streamInfo.srs.height
             ? `${streamInfo.srs.width}x${streamInfo.srs.height}` : '-' }}
        </span>
        <span class="bar-sep"></span>
        <span class="bar-item">
          {{ streamInfo.srs.fps != null ? `${streamInfo.srs.fps} fps` : '- fps' }}
        </span>
        <span class="bar-sep"></span>
        <span class="bar-item">
          {{ streamInfo.srs.kbps?.recv_30s != null
             ? `${(streamInfo.srs.kbps.recv_30s / 1000).toFixed(1)} Mbps` : '- Mbps' }}
        </span>
        <span class="bar-sep"></span>
        <span class="bar-item">
          {{ latencyInfo.speed }} KB/s
        </span>
        <span class="bar-sep"></span>
        <span class="bar-item">
          Drop: {{ latencyInfo.dropped }}/{{ latencyInfo.decoded }}
        </span>
        <span class="bar-sep"></span>
        <span class="bar-item">
          {{ streamInfo.srs.clients || 0 }} viewers
        </span>
      </div>
    </div>
  </div>
</template>

<style scoped>
.watch-view {
  max-width: 960px;
  margin: 0 auto;
}

.watch-header {
  display: flex;
  align-items: center;
  gap: 16px;
  margin-bottom: 20px;
}

.watch-info {
  display: flex;
  align-items: center;
  gap: 12px;
}

.watch-info h2 {
  font-size: 1.2rem;
  font-weight: 600;
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

.video-container {
  position: relative;
  background: #000;
  border-radius: var(--radius-lg);
  overflow: hidden;
  aspect-ratio: 16 / 9;
}

.video-player {
  width: 100%;
  height: 100%;
  object-fit: contain;
  display: block;
}

.video-overlay {
  position: absolute;
  inset: 0;
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  background: rgba(0, 0, 0, 0.7);
  color: var(--text-primary);
  gap: 16px;
  font-size: 0.95rem;
}

.video-overlay-error {
  color: var(--danger);
}

.stream-bar {
  position: absolute;
  bottom: 0;
  left: 0;
  right: 0;
  display: flex;
  align-items: center;
  flex-wrap: wrap;
  gap: 0;
  padding: 6px 14px;
  background: linear-gradient(transparent, rgba(0, 0, 0, 0.85));
  color: rgba(255, 255, 255, 0.9);
  font-family: 'SF Mono', 'Fira Code', 'Cascadia Code', monospace;
  font-size: 0.75rem;
  letter-spacing: 0.3px;
  pointer-events: none;
}

.bar-item {
  white-space: nowrap;
}

.bar-sep {
  width: 1px;
  height: 10px;
  margin: 0 10px;
  background: rgba(255, 255, 255, 0.3);
}

.bar-latency {
  font-weight: 700;
  padding: 1px 6px;
  border-radius: 3px;
}

.latency-good {
  color: #34d399;
  background: rgba(52, 211, 153, 0.15);
}

.latency-warn {
  color: #fbbf24;
  background: rgba(251, 191, 36, 0.15);
}

.latency-bad {
  color: #f87171;
  background: rgba(248, 113, 113, 0.2);
}
</style>
