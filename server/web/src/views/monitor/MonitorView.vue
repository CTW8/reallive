<script setup>
import { computed, nextTick, onBeforeUnmount, onMounted, ref, watch } from 'vue'
import { useRoute, useRouter } from 'vue-router'
import mpegts from 'mpegts.js'
import { useCameraStore } from '../../stores/camera.js'
import { cameraApi } from '../../api/index.js'

let echartsCore = null
let echartsLoadPromise = null

const router = useRouter()
const route = useRoute()
const cameraStore = useCameraStore()

const selectedCameraId = ref(null)
const streamInfo = ref(null)
const watchSessionId = ref('')
const playbackError = ref('')
const ptzZoom = ref(50)
const toastMessage = ref('')

const videoRef = ref(null)
let player = null
let refreshTimer = null
let heartbeatTimer = null
let metricTimer = null
let toastTimer = null
let resizeTimer = null

const streamUrl = ref('')
const latencyMs = ref(0)
const updatedLabel = ref('Updated just now')
const debugEnabled = ref(false)
const debugChartRef = ref(null)
const debugLocalSeries = ref([])
let debugChart = null

const selectedCamera = computed(() => {
  const id = Number(selectedCameraId.value)
  return cameraStore.cameras.find((c) => Number(c.id) === id) || null
})

const statusText = computed(() => {
  const status = selectedCamera.value?.status || 'offline'
  if (status === 'streaming') return 'Recording'
  if (status === 'online') return 'Online'
  return 'Offline'
})

const statusDotClass = computed(() => {
  const status = selectedCamera.value?.status || 'offline'
  if (status === 'streaming') return 'rec'
  if (status === 'online') return 'on'
  return 'off'
})

const topFps = computed(() => {
  // Prefer device-side SEI camera fps as the primary display value.
  // SRS fps can be bursty/noisy and may over-report momentarily.
  const camFps = Number(streamInfo.value?.sei?.cameraConfig?.fps)
  if (Number.isFinite(camFps) && camFps > 0) return camFps.toFixed(1)
  const srsFps = Number(streamInfo.value?.srs?.fps)
  if (Number.isFinite(srsFps) && srsFps > 0) {
    const clamped = Math.max(0, Math.min(60, srsFps))
    return clamped.toFixed(1)
  }
  return '0.0'
})

const topBitrate = computed(() => {
  const recv30s = Number(streamInfo.value?.srs?.kbps?.recv_30s)
  if (Number.isFinite(recv30s) && recv30s > 0) return `${(recv30s / 1000).toFixed(1)} Mbps`
  return '0.0 Mbps'
})

const debugTelemetry = computed(() => {
  const sei = streamInfo.value?.sei || null
  const seiHistory = Array.isArray(sei?.telemetryHistory) ? sei.telemetryHistory : []
  const history = seiHistory.length ? seiHistory : debugLocalSeries.value
  const latest = sei?.telemetry || {}
  const latestFromHistory = history.length ? history[history.length - 1] : null
  return {
    history,
    latest: {
      cpuPct: Number(latest.cpuPct ?? latestFromHistory?.cpuPct ?? 0) || 0,
      memoryPct: Number(latest.memoryPct ?? latestFromHistory?.memoryPct ?? 0) || 0,
      storagePct: Number(latest.storagePct ?? latestFromHistory?.storagePct ?? 0) || 0,
    },
  }
})

const hasDebugTelemetry = computed(() => {
  return Array.isArray(debugTelemetry.value.history) && debugTelemetry.value.history.length > 0
})

function pushLocalDebugSample(info) {
  const sei = info?.sei
  const telemetry = sei?.telemetry
  if (!telemetry || typeof telemetry !== 'object') return
  const ts = Number(sei?.updatedAt || Date.now())
  const cpuPct = Number(telemetry.cpuPct)
  const memoryPct = Number(telemetry.memoryPct)
  const storagePct = Number(telemetry.storagePct)
  if (!Number.isFinite(cpuPct) && !Number.isFinite(memoryPct) && !Number.isFinite(storagePct)) {
    return
  }
  const safePct = (v) => {
    const n = Number(v)
    if (!Number.isFinite(n)) return 0
    return Math.max(0, Math.min(100, n))
  }
  const series = debugLocalSeries.value.slice()
  const last = series[series.length - 1]
  if (last && Math.abs(Number(last.ts || 0) - ts) < 300) {
    last.cpuPct = safePct(cpuPct)
    last.memoryPct = safePct(memoryPct)
    last.storagePct = safePct(storagePct)
  } else {
    series.push({
      ts,
      cpuPct: safePct(cpuPct),
      memoryPct: safePct(memoryPct),
      storagePct: safePct(storagePct),
    })
  }
  if (series.length > 120) {
    series.splice(0, series.length - 120)
  }
  debugLocalSeries.value = series
}

async function ensureEchartsLoaded() {
  if (echartsCore) return echartsCore
  if (!echartsLoadPromise) {
    echartsLoadPromise = Promise.all([
      import('echarts/core'),
      import('echarts/charts'),
      import('echarts/components'),
      import('echarts/renderers'),
    ]).then(([core, charts, components, renderers]) => {
      core.use([
        charts.LineChart,
        components.GridComponent,
        components.TooltipComponent,
        components.LegendComponent,
        components.TitleComponent,
        renderers.CanvasRenderer,
      ])
      echartsCore = core
      return echartsCore
    })
  }
  return echartsLoadPromise
}

function destroyDebugChart() {
  if (debugChart) {
    try {
      debugChart.dispose()
    } catch {
    }
    debugChart = null
  }
}

function onWindowResize() {
  if (resizeTimer) clearTimeout(resizeTimer)
  resizeTimer = setTimeout(() => {
    try {
      debugChart?.resize()
    } catch {
    }
  }, 60)
}

async function renderDebugChart() {
  if (!debugEnabled.value || !debugChartRef.value) return
  const ec = await ensureEchartsLoaded()
  if (!debugChartRef.value) return
  if (!debugChart) {
    debugChart = ec.init(debugChartRef.value)
  }

  const history = debugTelemetry.value.history.slice(-60)
  const labels = history.map((p) => {
    const dt = new Date(Number(p.ts) || Date.now())
    return `${String(dt.getMinutes()).padStart(2, '0')}:${String(dt.getSeconds()).padStart(2, '0')}`
  })
  const cpu = history.map((p) => Number(p.cpuPct ?? 0) || 0)
  const mem = history.map((p) => Number(p.memoryPct ?? 0) || 0)
  const storage = history.map((p) => Number(p.storagePct ?? 0) || 0)

  debugChart.setOption({
    animation: false,
    grid: { left: 32, right: 14, top: 28, bottom: 24 },
    tooltip: { trigger: 'axis' },
    legend: {
      top: 2,
      textStyle: { color: '#cfd3f8', fontSize: 10 },
      itemWidth: 10,
      itemHeight: 6,
      data: ['CPU', 'MEM', 'DISK'],
    },
    xAxis: {
      type: 'category',
      data: labels,
      boundaryGap: false,
      axisLabel: { color: '#aeb4dd', fontSize: 9, interval: Math.max(0, Math.floor(labels.length / 6)) },
      axisLine: { lineStyle: { color: 'rgba(180,190,255,.25)' } },
      axisTick: { show: false },
    },
    yAxis: {
      type: 'value',
      min: 0,
      max: 100,
      axisLabel: { color: '#aeb4dd', fontSize: 9, formatter: '{value}%' },
      splitLine: { lineStyle: { color: 'rgba(180,190,255,.12)' } },
    },
    series: [
      { name: 'CPU', type: 'line', data: cpu, showSymbol: false, lineStyle: { width: 1.4, color: '#ff8f70' } },
      { name: 'MEM', type: 'line', data: mem, showSymbol: false, lineStyle: { width: 1.4, color: '#69a6ff' } },
      { name: 'DISK', type: 'line', data: storage, showSymbol: false, lineStyle: { width: 1.4, color: '#78df8b' } },
    ],
  }, true)
}

function toggleDebugOverlay() {
  debugEnabled.value = !debugEnabled.value
}

function resolvePreferredCameraId() {
  const raw = route.params?.id ?? route.query?.cameraId
  const preferred = Number(raw)
  if (Number.isFinite(preferred) && preferred > 0) return preferred
  return null
}

function showToast(message) {
  toastMessage.value = message
  if (toastTimer) clearTimeout(toastTimer)
  toastTimer = setTimeout(() => {
    toastMessage.value = ''
    toastTimer = null
  }, 1600)
}

async function startWatchSession(cameraId = null) {
  const id = Number(cameraId ?? selectedCamera.value?.id)
  if (!Number.isFinite(id)) return
  try {
    const state = await cameraApi.startWatchSession(id)
    watchSessionId.value = String(state?.sessionId || state?.session_id || '')
  } catch (err) {
    playbackError.value = err?.message || 'Failed to start watch session'
  }
}

async function heartbeatWatchSession(cameraId = null) {
  const id = Number(cameraId ?? selectedCamera.value?.id)
  if (!Number.isFinite(id) || !watchSessionId.value) return
  try {
    await cameraApi.heartbeatWatchSession(id, watchSessionId.value)
  } catch {
  }
}

async function stopWatchSession(cameraId = null) {
  const id = Number(cameraId ?? selectedCamera.value?.id)
  if (!Number.isFinite(id) || !watchSessionId.value) return
  try {
    await cameraApi.stopWatchSession(id, watchSessionId.value)
  } catch {
  }
  watchSessionId.value = ''
}

async function refreshStreamInfo() {
  if (!selectedCamera.value) return
  try {
    const info = await cameraApi.getStreamInfo(selectedCamera.value.id)
    streamInfo.value = info
    pushLocalDebugSample(info)
    updatedLabel.value = 'Updated just now'
    const key = info?.stream_key || selectedCamera.value?.stream_key
    if (key) {
      streamUrl.value = `${window.location.origin}/live/${encodeURIComponent(key)}.flv`
    }
  } catch (err) {
    playbackError.value = err?.message || 'Failed to load stream info'
  }
}

function destroyPlayer() {
  if (player) {
    try {
      player.pause()
      player.unload()
      player.detachMediaElement()
      player.destroy()
    } catch {
    }
    player = null
  }
}

async function initPlayer() {
  destroyPlayer()
  if (!videoRef.value || !streamUrl.value) return

  if (!mpegts.getFeatureList().mseLivePlayback) {
    playbackError.value = 'Browser does not support MSE live playback'
    return
  }

  try {
    player = mpegts.createPlayer({
      type: 'flv',
      url: streamUrl.value,
      isLive: true,
      hasAudio: false,
      hasVideo: true,
    }, {
      enableWorker: true,
      stashInitialSize: 64,
      lazyLoad: false,
      liveBufferLatencyChasing: true,
      liveBufferLatencyMaxLatency: 0.8,
      liveBufferLatencyMinRemain: 0.2,
    })

    player.attachMediaElement(videoRef.value)
    player.load()
    player.play().catch(() => {})
    playbackError.value = ''
  } catch (err) {
    playbackError.value = err?.message || 'Failed to initialize player'
  }
}

function updateLatency() {
  const video = videoRef.value
  if (!video || !video.buffered || !video.buffered.length) {
    latencyMs.value = 0
    return
  }
  try {
    const end = video.buffered.end(video.buffered.length - 1)
    const delay = Math.max(0, end - video.currentTime)
    latencyMs.value = Math.round(delay * 1000)
  } catch {
    latencyMs.value = 0
  }
}

function pickCamera(cameraId) {
  selectedCameraId.value = Number(cameraId)
}

async function reconnectPlayer() {
  await refreshStreamInfo()
  await initPlayer()
  showToast('Reconnected')
}

function onLiveAction(action) {
  if (action === 'playback') {
    router.push('/playback')
    return
  }
  if (action === 'alerts') {
    router.push('/alerts')
    return
  }
  if (action === 'camera-config') {
    router.push('/settings')
    return
  }
  if (action === 'fullscreen') {
    videoRef.value?.requestFullscreen?.().catch?.(() => {})
    return
  }
  showToast(`${action} action queued`)
}

function onPtz(dir) {
  showToast(`PTZ ${dir}`)
}

function adjustZoom(delta) {
  const next = Math.max(0, Math.min(100, Number(ptzZoom.value) + Number(delta)))
  ptzZoom.value = next
  showToast(`PTZ zoom ${next}%`)
}

watch(selectedCameraId, async (next, prev) => {
  const prevId = Number(prev)
  const nextId = Number(next)
  if (Number.isFinite(prevId) && Number.isFinite(nextId) && prevId !== nextId) {
    await stopWatchSession(prevId)
  }
  await refreshStreamInfo()
  await startWatchSession(nextId)
  await initPlayer()
})

onMounted(async () => {
  await cameraStore.fetchCameras()
  if (!cameraStore.cameras.length) return

  const preferred = resolvePreferredCameraId()
  const matched = preferred != null
    ? cameraStore.cameras.find((camera) => Number(camera.id) === preferred)
    : null
  selectedCameraId.value = Number((matched || cameraStore.cameras[0]).id)
  await refreshStreamInfo()
  await startWatchSession()
  await initPlayer()

  refreshTimer = setInterval(() => {
    refreshStreamInfo()
  }, 5000)

  heartbeatTimer = setInterval(() => {
    heartbeatWatchSession()
  }, 10000)

  metricTimer = setInterval(() => {
    updateLatency()
    if (updatedLabel.value === 'Updated just now') {
      updatedLabel.value = 'Updated moments ago'
    } else {
      updatedLabel.value = 'Updated recently'
    }
  }, 1000)
  window.addEventListener('resize', onWindowResize)
})

watch(debugEnabled, async (enabled) => {
  if (!enabled) {
    destroyDebugChart()
    return
  }
  await nextTick()
  await renderDebugChart()
})

watch(
  () => streamInfo.value?.sei?.updatedAt,
  async () => {
    if (!debugEnabled.value) return
    await nextTick()
    await renderDebugChart()
  }
)

watch(
  () => route.params?.id,
  (next) => {
    const id = Number(next)
    if (!Number.isFinite(id) || id <= 0) return
    if (Number(selectedCameraId.value) === id) return
    const matched = cameraStore.cameras.find((camera) => Number(camera.id) === id)
    if (!matched) return
    selectedCameraId.value = id
  }
)

onBeforeUnmount(async () => {
  if (refreshTimer) clearInterval(refreshTimer)
  if (heartbeatTimer) clearInterval(heartbeatTimer)
  if (metricTimer) clearInterval(metricTimer)
  if (toastTimer) clearTimeout(toastTimer)
  if (resizeTimer) clearTimeout(resizeTimer)
  window.removeEventListener('resize', onWindowResize)
  await stopWatchSession()
  destroyPlayer()
  destroyDebugChart()
})
</script>

<template>
  <div class="monitor-page">
    <div class="monitor-toolbar">
      <div class="mt-left">
        <span class="mi" style="font-size:18px;color:var(--on-sfv)">filter_list</span>
        <select id="monitorCameraSelect" v-model.number="selectedCameraId" class="monitor-select">
          <option v-for="camera in cameraStore.cameras" :key="camera.id" :value="camera.id">
            {{ camera.name }} · {{ camera.status }}
          </option>
        </select>
      </div>
      <div class="mt-right">
        <div class="monitor-status-strip">
          <span class="ms-chip"><span class="mi" style="font-size:16px">videocam</span> <strong id="monitorTopCamName">{{ selectedCamera?.name || 'No camera' }}</strong></span>
          <span class="ms-chip"><span id="monitorTopStatusDot" class="dot" :class="statusDotClass"></span><span id="monitorTopStatusText">{{ statusText }}</span></span>
          <span class="ms-chip"><span class="mi" style="font-size:16px">animation</span>FPS <strong id="monitorTopFps">{{ topFps }}</strong></span>
          <span class="ms-chip"><span class="mi" style="font-size:16px">network_check</span><strong id="monitorTopBitrate">{{ topBitrate }}</strong></span>
          <span class="ms-chip"><span class="mi" style="font-size:16px">timer</span><strong id="monitorTopLatency">{{ latencyMs }} ms</strong></span>
          <span class="ms-chip"><span class="mi" style="font-size:16px">update</span><span id="monitorTopUpdatedAt">{{ updatedLabel }}</span></span>
        </div>
        <div class="mt-actions">
          <button class="mt-btn" data-monitor-top-action="refresh" @click="refreshStreamInfo"><span class="mi">refresh</span> Refresh</button>
          <button class="mt-btn" data-monitor-top-action="reconnect" @click="reconnectPlayer"><span class="mi">sync</span> Reconnect</button>
          <button class="mt-btn" data-monitor-top-action="debug" @click="toggleDebugOverlay"><span class="mi">monitoring</span> {{ debugEnabled ? 'Hide Debug' : 'Debug' }}</button>
          <button class="mt-btn" data-monitor-top-action="settings" @click="router.push('/settings')"><span class="mi">settings</span> Device Settings</button>
        </div>
      </div>
    </div>

    <div class="live-layout">
      <div class="monitor-content">
        <div id="videoGrid" class="video-grid g1x1">
          <div class="video-cell active" :data-camera="selectedCamera?.name || ''" :data-status="selectedCamera?.status || 'offline'" draggable="false" @click="selectedCamera && pickCamera(selectedCamera.id)">
            <template v-if="selectedCamera">
              <div class="sv-lines"></div>
              <div class="sv-grid"></div>
              <div class="sv-icon"><span class="mi">videocam</span></div>
              <video ref="videoRef" class="monitor-video" muted playsinline autoplay></video>
              <div v-if="debugEnabled" class="debug-overlay">
                <div class="debug-head">
                  <span class="debug-tag">DEBUG</span>
                  <span>FPS {{ topFps }}</span>
                  <span>Latency {{ latencyMs }} ms</span>
                </div>
                <div class="debug-stats">
                  <span>CPU {{ debugTelemetry.latest.cpuPct.toFixed(1) }}%</span>
                  <span>MEM {{ debugTelemetry.latest.memoryPct.toFixed(1) }}%</span>
                  <span>DISK {{ debugTelemetry.latest.storagePct.toFixed(1) }}%</span>
                </div>
                <div v-if="!hasDebugTelemetry" class="debug-empty">Waiting for SEI telemetry...</div>
                <div ref="debugChartRef" class="debug-chart"></div>
              </div>
              <div class="scan"></div>
              <div class="vc-top">
                <span class="cam-name">{{ selectedCamera.name }}</span>
                <div v-if="selectedCamera.status === 'streaming'" class="live-badge"><span class="dot blink"></span> REC</div>
                <div v-else-if="selectedCamera.status === 'offline'" class="offline-badge">OFFLINE</div>
              </div>
              <div class="vc-bottom">
                <span class="ts">{{ new Date().toLocaleString() }}</span>
              </div>
              <div v-if="selectedCamera.status === 'offline'" class="offline-overlay"><span class="mi">videocam_off</span><span>Offline</span></div>
            </template>
            <template v-else>
              <div class="empty-hint"><span class="mi">videocam_off</span><span>No camera available</span></div>
            </template>
          </div>
        </div>
        <div v-if="playbackError" class="error-msg">{{ playbackError }}</div>
      </div>

      <aside class="live-side-panel">
        <h4>Live Controls</h4>
        <div class="live-action-list">
          <button class="live-action-btn" data-live-action="playback" @click="onLiveAction('playback')"><span class="mi">history</span>Playback</button>
          <button class="live-action-btn" data-live-action="snapshot" @click="onLiveAction('snapshot')"><span class="mi">photo_camera</span>Snapshot</button>
          <button class="live-action-btn" data-live-action="share" @click="onLiveAction('share')"><span class="mi">share</span>Share</button>
          <button class="live-action-btn" data-live-action="alerts" @click="onLiveAction('alerts')"><span class="mi">notifications_active</span>Alerts</button>
          <button class="live-action-btn" data-live-action="camera-config" @click="onLiveAction('camera-config')"><span class="mi">settings</span>Camera Config</button>
          <button class="live-action-btn" data-live-action="fullscreen" @click="onLiveAction('fullscreen')"><span class="mi">fullscreen</span>Fullscreen</button>
        </div>

        <div id="ptzPanel" class="ptz-panel">
          <div class="ptz-head">
            <div>
              <div class="title">PTZ Control</div>
              <div id="ptzCameraName" class="sub">{{ selectedCamera?.name || 'No camera selected' }}</div>
            </div>
            <span class="ptz-status">PTZ Ready</span>
          </div>
          <div class="ptz-pad-wrap">
            <div class="ptz-pad">
              <button class="ptz-btn up" @click="onPtz('up')"><span class="mi">keyboard_arrow_up</span></button>
              <button class="ptz-btn dn" @click="onPtz('down')"><span class="mi">keyboard_arrow_down</span></button>
              <button class="ptz-btn lt" @click="onPtz('left')"><span class="mi">keyboard_arrow_left</span></button>
              <button class="ptz-btn rt" @click="onPtz('right')"><span class="mi">keyboard_arrow_right</span></button>
              <button class="ptz-center" @click="onPtz('center')"><span class="mi">center_focus_strong</span></button>
            </div>
          </div>
          <div class="ptz-zoom">
            <button class="zb" @click="adjustZoom(-10)"><span class="mi">remove</span></button>
            <div class="zoom-wrap">
              <div class="zoom-label">Zoom</div>
              <div class="zoom-bar"><div class="zoom-fill" :style="{ width: `${ptzZoom}%` }"></div></div>
            </div>
            <button class="zb" @click="adjustZoom(10)"><span class="mi">add</span></button>
          </div>
        </div>
      </aside>
    </div>

    <div v-if="toastMessage" class="monitor-toast">{{ toastMessage }}</div>
  </div>
</template>

<style scoped>
.monitor-page { display: flex; flex-direction: column; gap: 0; height: calc(100vh - var(--topbar-h) - 24px); }
.monitor-toolbar { display: flex; align-items: center; justify-content: space-between; margin-bottom: 16px; gap: 12px; flex-wrap: wrap; }
.monitor-toolbar .mt-left { display: flex; align-items: center; gap: 8px; flex-wrap: wrap; }
.monitor-toolbar .mt-right { display: flex; align-items: center; gap: 8px; flex-wrap: wrap; justify-content: flex-end; }
.monitor-select { height: 34px; padding: 0 10px; border-radius: var(--r2); border: 1px solid var(--olv); background: var(--sc2); color: var(--on-sf); font: 400 13px/18px 'Roboto', sans-serif; min-width: 220px; outline: none; }
.monitor-status-strip { display: flex; align-items: center; gap: 8px; flex-wrap: wrap; }
.ms-chip { height: 30px; padding: 0 10px; border-radius: var(--r6); border: 1px solid var(--olv); background: var(--sc); display: inline-flex; align-items: center; gap: 6px; font: 500 12px/16px 'Roboto', sans-serif; color: var(--on-sfv); }
.ms-chip .dot { width: 7px; height: 7px; border-radius: 50%; }
.ms-chip .dot.on { background: var(--green); }
.ms-chip .dot.off { background: var(--red); }
.ms-chip .dot.rec { background: var(--orange); }
.ms-chip strong { color: var(--on-sf); }
.monitor-toolbar .mt-actions { display: flex; gap: 8px; align-items: center; }
.mt-btn { height: 34px; padding: 0 14px; border-radius: var(--r6); border: 1px solid var(--olv); background: transparent; color: var(--on-sfv); font: 500 13px/18px 'Roboto', sans-serif; cursor: pointer; display: flex; align-items: center; gap: 6px; }
.mt-btn:hover { background: var(--sc2); }
.mt-btn .mi { font-size: 18px; }

.live-layout {
  display: grid;
  grid-template-columns: minmax(0, 1fr) 280px;
  gap: 12px;
  height: calc(100vh - var(--topbar-h) - 48px - 24px);
  min-height: 0;
}
.monitor-content {
  display: flex;
  flex-direction: column;
  height: calc(100vh - var(--topbar-h) - 48px - 52px - 24px);
  position: relative;
  min-width: 0;
  min-height: 0;
}
.video-grid { display: grid; gap: 4px; flex: 1; min-height: 0; }
.video-grid.g1x1 { grid-template-columns: 1fr; grid-template-rows: 1fr; }

.video-cell { position: relative; background: linear-gradient(135deg, #0d0d15, #1c1728); border-radius: var(--r2); overflow: hidden; min-height: 0; display: flex; align-items: center; justify-content: center; cursor: pointer; transition: outline .15s; }
.video-cell:hover { outline: 2px solid var(--pri); outline-offset: -2px; }
.video-cell.active { outline: 2px solid var(--pri); outline-offset: -2px; }
.video-cell .sv-lines { position: absolute; inset: 0; background: repeating-linear-gradient(0deg, transparent, transparent 2px, rgba(255,255,255,.01) 2px, rgba(255,255,255,.01) 4px); pointer-events: none; }
.video-cell .sv-grid { position: absolute; inset: 0; background: linear-gradient(rgba(255,255,255,.015) 1px, transparent 1px), linear-gradient(90deg, rgba(255,255,255,.015) 1px, transparent 1px); background-size: 20px 20px; pointer-events: none; }
.video-cell .sv-icon { position: absolute; inset: 0; display: flex; align-items: center; justify-content: center; }
.video-cell .sv-icon .mi { font-size: 42px; color: rgba(255,255,255,.04); }
.monitor-video { position: absolute; inset: 0; width: 100%; height: 100%; object-fit: contain; z-index: 1; background: #000; }
.debug-overlay {
  position: absolute;
  top: 8px;
  right: 8px;
  z-index: 4;
  width: 360px;
  max-width: calc(100% - 16px);
  background: rgba(9, 12, 22, .78);
  border: 1px solid rgba(170, 184, 255, .28);
  border-radius: 10px;
  backdrop-filter: blur(6px);
  padding: 8px;
}
.debug-head {
  display: flex;
  align-items: center;
  gap: 10px;
  color: #e2e7ff;
  font: 500 11px/14px 'Roboto', sans-serif;
  margin-bottom: 6px;
}
.debug-tag {
  padding: 2px 6px;
  border-radius: 999px;
  background: rgba(255, 122, 122, .22);
  color: #ffb0b0;
  border: 1px solid rgba(255, 122, 122, .45);
}
.debug-stats {
  display: flex;
  align-items: center;
  gap: 10px;
  color: #b8c1f7;
  font: 500 10px/14px 'Roboto', sans-serif;
  margin-bottom: 4px;
}
.debug-empty {
  color: #9aa5e9;
  font: 500 10px/14px 'Roboto', sans-serif;
  margin-bottom: 4px;
}
.debug-chart {
  width: 100%;
  height: 140px;
}
.scan { position: absolute; top: 0; left: 0; right: 0; height: 2px; background: rgba(200,191,255,.06); animation: scan 4s linear infinite; z-index: 2; }
.vc-top { position: absolute; top: 0; left: 0; right: 0; padding: 8px 10px; display: flex; justify-content: space-between; align-items: flex-start; background: linear-gradient(to bottom, rgba(0,0,0,.6), transparent); z-index: 3; }
.cam-name { font: 500 12px/18px 'Roboto', sans-serif; color: #fff; }
.live-badge { display: flex; align-items: center; gap: 4px; padding: 2px 7px; border-radius: var(--r1); background: rgba(200,60,60,.9); font: 500 9px/14px 'Roboto', sans-serif; color: #fff; }
.live-badge .dot { width: 4px; height: 4px; border-radius: 50%; background: #fff; }
.offline-badge { padding: 2px 7px; border-radius: var(--r1); background: rgba(80,80,80,.9); font: 500 9px/14px 'Roboto', sans-serif; color: #aaa; }
.vc-bottom { position: absolute; bottom: 0; left: 0; right: 0; padding: 8px 10px; background: linear-gradient(to top, rgba(0,0,0,.6), transparent); display: flex; justify-content: space-between; align-items: center; z-index: 3; }
.ts { font: 400 11px/16px 'Roboto', sans-serif; color: rgba(255,255,255,.7); }
.empty-hint { height: 100%; display: flex; flex-direction: column; align-items: center; justify-content: center; gap: 8px; color: var(--ol); font: 400 13px/18px 'Roboto', sans-serif; }
.empty-hint .mi { font-size: 32px; color: var(--olv); }
.offline-overlay { position: absolute; inset: 0; display: flex; flex-direction: column; align-items: center; justify-content: center; background: rgba(15,13,19,.8); z-index: 2; }
.offline-overlay .mi { font-size: 32px; color: var(--red); margin-bottom: 6px; }
.offline-overlay span { font: 500 12px/16px 'Roboto', sans-serif; color: var(--red); }

.live-side-panel {
  background: var(--sc);
  border-radius: var(--r3);
  padding: 12px;
  display: flex;
  flex-direction: column;
  gap: 10px;
  min-width: 0;
}
.live-side-panel h4 { font: 500 14px/20px 'Roboto', sans-serif; }
.live-action-list { display: grid; grid-template-columns: 1fr 1fr; gap: 8px; }
.live-action-btn { height: 68px; padding: 8px 6px; border-radius: var(--r2); border: 1px solid var(--olv); background: transparent; color: var(--on-sfv); font: 500 12px/16px 'Roboto', sans-serif; display: flex; flex-direction: column; align-items: center; justify-content: center; gap: 6px; cursor: pointer; text-align: center; }
.live-action-btn:hover { background: var(--sc2); }
.live-action-btn .mi { font-size: 18px; }

.ptz-panel { width: 100%; background: linear-gradient(180deg, var(--sc2), rgba(25,22,33,.95)); border: 1px solid var(--olv); border-radius: var(--r3); padding: 12px; display: flex; flex-direction: column; gap: 12px; }
.ptz-head { display: flex; align-items: center; justify-content: space-between; gap: 8px; }
.ptz-head .title { font: 500 13px/18px 'Roboto', sans-serif; color: #fff; }
.ptz-head .sub { font: 400 11px/16px 'Roboto', sans-serif; color: var(--on-sfv); }
.ptz-status { font: 500 11px/16px 'Roboto', sans-serif; color: var(--pri); padding: 2px 8px; border-radius: var(--r6); background: rgba(200,191,255,.14); border: 1px solid rgba(200,191,255,.3); }
.ptz-pad-wrap { display: flex; justify-content: center; }
.ptz-pad { width: 148px; height: 148px; border-radius: 50%; border: 1px solid rgba(255,255,255,.14); position: relative; background: radial-gradient(circle at 50% 42%, rgba(200,191,255,.18), rgba(16,14,22,.92)); box-shadow: inset 0 0 18px rgba(200,191,255,.12); }
.ptz-btn { position: absolute; width: 38px; height: 38px; border-radius: 50%; border: 1px solid rgba(255,255,255,.2); background: rgba(255,255,255,.08); color: #fff; display: flex; align-items: center; justify-content: center; cursor: pointer; }
.ptz-btn:hover { background: rgba(200,191,255,.26); border-color: rgba(200,191,255,.5); }
.ptz-btn .mi { font-size: 22px; }
.ptz-btn.up { top: 10px; left: 50%; transform: translateX(-50%); }
.ptz-btn.dn { bottom: 10px; left: 50%; transform: translateX(-50%); }
.ptz-btn.lt { left: 10px; top: 50%; transform: translateY(-50%); }
.ptz-btn.rt { right: 10px; top: 50%; transform: translateY(-50%); }
.ptz-center { position: absolute; left: 50%; top: 50%; transform: translate(-50%, -50%); width: 54px; height: 54px; border-radius: 50%; border: 1px solid rgba(200,191,255,.5); background: rgba(200,191,255,.18); color: #fff; display: flex; align-items: center; justify-content: center; cursor: pointer; }
.ptz-center:hover { background: rgba(200,191,255,.3); }
.ptz-center .mi { font-size: 22px; }
.ptz-zoom { display: flex; align-items: center; gap: 10px; }
.ptz-zoom .zb { width: 32px; height: 32px; border-radius: 50%; background: rgba(255,255,255,.08); border: 1px solid rgba(255,255,255,.12); display: flex; align-items: center; justify-content: center; cursor: pointer; }
.ptz-zoom .zb:hover { background: rgba(255,255,255,.15); }
.ptz-zoom .zb .mi { font-size: 18px; color: var(--on-sfv); }
.zoom-wrap { flex: 1; }
.zoom-label { font: 500 11px/16px 'Roboto', sans-serif; color: var(--on-sfv); margin-bottom: 4px; }
.zoom-bar { height: 5px; border-radius: 6px; background: var(--olv); position: relative; }
.zoom-fill { height: 100%; border-radius: 6px; background: linear-gradient(90deg, var(--pri), #88d5ff); }

.monitor-toast { position: fixed; right: 20px; bottom: 22px; z-index: 500; background: rgba(30, 27, 38, 0.96); border: 1px solid var(--olv); border-radius: var(--r2); padding: 8px 12px; font: 500 12px/16px 'Roboto', sans-serif; color: var(--on-sf); box-shadow: var(--e2); }
.error-msg { margin-top: 8px; color: var(--red); font: 500 12px/16px 'Roboto', sans-serif; }
.blink { animation: blink 1.3s infinite; }

@keyframes scan {
  0% { transform: translateY(0); }
  100% { transform: translateY(100%); }
}

@keyframes blink {
  0%, 60%, 100% { opacity: 1; }
  30% { opacity: .3; }
}

@media (max-width: 1439px) {
  .live-layout { grid-template-columns: minmax(0, 1fr) 260px; }
}

@media (max-width: 1023px) {
  .live-layout { grid-template-columns: 1fr; height: auto; }
  .video-grid { min-height: 56vh; }
}

@media (max-width: 768px) {
  .monitor-status-strip { display: grid; grid-template-columns: 1fr 1fr; }
  .live-action-list { grid-template-columns: 1fr; }
  .debug-overlay { width: calc(100% - 12px); right: 6px; top: 6px; padding: 6px; }
  .debug-chart { height: 120px; }
  .debug-head, .debug-stats { gap: 6px; }
}
</style>
