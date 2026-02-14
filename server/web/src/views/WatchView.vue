<script setup>
import { ref, computed, onMounted, onBeforeUnmount, nextTick, watch } from 'vue'
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

const playbackMode = ref('live')
const historyOverview = ref(null)
const historyTimeline = ref({
  startMs: null,
  endMs: null,
  ranges: [],
  thumbnails: [],
  segments: [],
  nowMs: null,
})
const historySelectedMs = ref(null)
const timelineDragMs = ref(null)
const historyLoading = ref(false)
const historyError = ref('')
const nowMs = ref(Date.now())
const historyReplaySessionId = ref(null)
const historyReplayTransport = ref('')
const timelineZoomPct = ref(35)
const timelineZoomAnchorMs = ref(null)
const telemetryChartRef = ref(null)

const videoRef = ref(null)
let player = null
let socket = null
let refreshTimer = null
let latencyTimer = null
let historyRefreshTimer = null
let telemetryChartInstance = null
let timelineZoomTimer = null
let echartsCore = null
let echartsLoadPromise = null
const blackThumbUrlCache = new Set()
const normalThumbUrlCache = new Set()
const blockedThumbUrls = ref({})

const liveFlvUrl = ref('')

const latencyInfo = ref({
  buffer: 0,
  e2e: null,
  dropped: 0,
  decoded: 0,
  speed: 0,
})

const telemetryInfo = computed(() => streamInfo.value?.sei?.telemetry || null)
const telemetryHistory = computed(() => {
  const history = streamInfo.value?.sei?.telemetryHistory || []
  return history.slice(-90)
})
const telemetryPoints = computed(() => {
  if (telemetryHistory.value.length) {
    return telemetryHistory.value
  }
  if (!telemetryInfo.value) {
    return []
  }
  return [{
    ts: Date.now(),
    cpuPct: telemetryInfo.value.cpuPct,
    memoryPct: telemetryInfo.value.memoryPct,
    storagePct: telemetryInfo.value.storagePct,
  }]
})
const telemetryUpdatedAt = computed(() => streamInfo.value?.sei?.updatedAt || null)
const cameraSeiConfig = computed(() => streamInfo.value?.sei?.cameraConfig || null)
const configurableSeiConfig = computed(() => streamInfo.value?.sei?.configurable || null)
const telemetryStateLabel = computed(() => {
  if (!telemetryInfo.value) return 'SEI offline'
  if (telemetryHistory.value.length >= 2) return `SEI online · ${telemetryHistory.value.length} pts`
  return 'SEI online · single point'
})

const isLiveMode = computed(() => playbackMode.value === 'live')
const isHistoryMode = computed(() => playbackMode.value === 'history')

const hasHistory = computed(() => {
  return !!historyOverview.value?.hasHistory && historyTimeline.value.startMs != null
})

const timelineGrowthDeltaMs = computed(() => {
  if (!historyOverview.value?.hasActiveRecording || !isLiveMode.value) return 0
  const baseNow = Number(historyOverview.value?.nowMs || 0)
  if (!Number.isFinite(baseNow) || baseNow <= 0) return 0
  return Math.max(0, nowMs.value - baseNow)
})

const timelineMinMs = computed(() => {
  if (historyTimeline.value.startMs != null) return historyTimeline.value.startMs
  if (historyOverview.value?.timeRange?.startMs != null) return historyOverview.value.timeRange.startMs
  return nowMs.value - 3600000
})

const timelineMaxMs = computed(() => {
  const baseEnd = historyTimeline.value.endMs != null
    ? historyTimeline.value.endMs
    : (historyOverview.value?.timeRange?.endMs != null ? historyOverview.value.timeRange.endMs : nowMs.value)
  return baseEnd + timelineGrowthDeltaMs.value
})

const selectedTimelineMs = computed(() => {
  if (timelineDragMs.value != null) return timelineDragMs.value
  if (historySelectedMs.value != null) return historySelectedMs.value
  return nowMs.value
})

const timelineThumbTiles = computed(() => {
  const min = timelineMinMs.value
  const max = timelineMaxMs.value
  const span = Math.max(1, max - min)
  const explicitThumbs = (historyTimeline.value.thumbnails || [])
    .map((thumb) => ({ ts: Number(thumb.ts), url: thumb.url || null }))
    .filter((thumb) => Number.isFinite(thumb.ts))
  const segmentThumbs = (historyTimeline.value.segments || [])
    .filter((segment) => segment && segment.thumbnailUrl)
    .map((segment) => ({
      ts: Math.floor((Number(segment.startMs || 0) + Number(segment.endMs || 0)) / 2),
      url: segment.thumbnailUrl,
    }))
    .filter((thumb) => Number.isFinite(thumb.ts))
  const thumbs = (explicitThumbs.length ? explicitThumbs : segmentThumbs)
    .sort((a, b) => a.ts - b.ts)
  const tileCount = 24

  if (!thumbs.length) {
    return Array.from({ length: tileCount }, (_, i) => ({
      id: `tile-empty-${i}`,
      thumbnailUrl: null,
    }))
  }

  let cursor = 0
  return Array.from({ length: tileCount }, (_, i) => {
    const centerTs = min + ((i + 0.5) / tileCount) * span
    while (cursor + 1 < thumbs.length) {
      const currentDiff = Math.abs(thumbs[cursor].ts - centerTs)
      const nextDiff = Math.abs(thumbs[cursor + 1].ts - centerTs)
      if (nextDiff > currentDiff) break
      cursor += 1
    }
    return {
      id: `tile-${i}`,
      ts: Math.round(centerTs),
      thumbnailUrl: thumbs[cursor].url,
    }
  })
})

const timelineCursorLeft = computed(() => {
  const min = timelineMinMs.value
  const max = timelineMaxMs.value
  const span = Math.max(1, max - min)
  const pct = ((selectedTimelineMs.value - min) / span) * 100
  return Math.max(0, Math.min(100, pct))
})

const timelineTickMarks = computed(() => {
  const min = timelineMinMs.value
  const max = timelineMaxMs.value
  const span = Math.max(1, max - min)
  const majorCount = 7
  const marks = []

  for (let i = 0; i < majorCount; i += 1) {
    const ratio = i / (majorCount - 1)
    const left = ratio * 100
    const ts = Math.round(min + span * ratio)
    marks.push({
      id: `major-${i}-${ts}`,
      ts,
      left,
      major: true,
      label: span >= 24 * 3600 * 1000
        ? new Date(ts).toLocaleString([], { month: '2-digit', day: '2-digit', hour: '2-digit', minute: '2-digit' })
        : new Date(ts).toLocaleTimeString([], { hour: '2-digit', minute: '2-digit', second: '2-digit' }),
    })

    if (i < majorCount - 1) {
      const nextLeft = ((i + 1) / (majorCount - 1)) * 100
      marks.push({
        id: `minor-${i}`,
        ts: null,
        left: (left + nextLeft) / 2,
        major: false,
        label: '',
      })
    }
  }
  return marks
})

const displayTotalDurationMs = computed(() => {
  const base = Number(historyOverview.value?.totalDurationMs || 0)
  return base + timelineGrowthDeltaMs.value
})

const timelineLabel = computed(() => {
  return formatDateTime(selectedTimelineMs.value)
})

const timelineWindowMs = computed(() => {
  const minWindowMs = 2 * 60 * 1000
  const maxWindowMs = 6 * 3600 * 1000
  const t = Math.max(0, Math.min(100, Number(timelineZoomPct.value))) / 100
  return Math.round(maxWindowMs * Math.pow(minWindowMs / maxWindowMs, t))
})

const timelineWindowLabel = computed(() => formatDuration(timelineWindowMs.value))

const timelineSliderStepMs = computed(() => {
  const span = Math.max(1, timelineMaxMs.value - timelineMinMs.value)
  return Math.max(200, Math.round(span / 360))
})

const historyStatusLabel = computed(() => {
  if (!hasHistory.value) return 'No history'
  if (historyLoading.value) return 'Loading...'
  if (isLiveMode.value) return 'Live'
  return 'History'
})

const hasTelemetryData = computed(() => telemetryPoints.value.length > 0)

const cameraConfigItems = computed(() => {
  const camera = cameraSeiConfig.value || null
  const srs = streamInfo.value?.srs || null
  if (!camera && !srs) return []
  const fallbackBitrate = Number(srs?.kbps?.recv_30s)
  const mergedBitrate = camera?.bitrate ?? (Number.isFinite(fallbackBitrate) ? fallbackBitrate * 1000 : null)
  return [
    {
      key: 'Resolution',
      value: (camera?.width && camera?.height)
        ? `${camera.width}x${camera.height}`
        : (srs?.width && srs?.height ? `${srs.width}x${srs.height}` : '-'),
    },
    { key: 'FPS', value: formatOptionalNumber(camera?.fps ?? srs?.fps, 1) },
    { key: 'Pixel Format', value: camera?.pixel_format || '-' },
    { key: 'Codec', value: camera?.codec || srs?.codec || '-' },
    { key: 'Bitrate', value: formatBitrate(mergedBitrate) },
    { key: 'Profile', value: camera?.profile || srs?.profile || '-' },
    { key: 'GOP', value: camera?.gop != null ? String(camera.gop) : '-' },
    { key: 'Audio', value: camera?.audio_enabled == null ? '-' : (camera.audio_enabled ? 'Enabled' : 'Disabled') },
    { key: 'Source', value: camera ? 'SEI' : 'SRS Fallback' },
  ]
})

const configurableItems = computed(() => {
  const configurable = configurableSeiConfig.value
  if (!configurable) return []
  return [
    { key: 'Resolution', value: formatConfigValue(configurable.resolution) },
    { key: 'FPS', value: formatConfigValue(configurable.fps) },
    { key: 'Profile', value: formatConfigValue(configurable.profile) },
    { key: 'Bitrate', value: formatConfigValue(configurable.bitrate) },
    { key: 'GOP', value: formatConfigValue(configurable.gop) },
  ]
})

function formatTelemetryAxisTime(ts) {
  const n = Number(ts)
  if (!Number.isFinite(n) || n <= 0) return ''
  return new Date(n).toLocaleTimeString([], { hour12: false, minute: '2-digit', second: '2-digit' })
}

function toPct(value) {
  const n = Number(value)
  if (!Number.isFinite(n)) return 0
  return Math.max(0, Math.min(100, n))
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
        renderers.CanvasRenderer,
      ])
      echartsCore = core
      return echartsCore
    })
  }
  return echartsLoadPromise
}

async function initTelemetryChart() {
  if (!telemetryChartRef.value) return
  if (telemetryChartInstance) return
  const ec = await ensureEchartsLoaded()
  telemetryChartInstance = ec.init(telemetryChartRef.value)
}

function resizeTelemetryChart() {
  if (telemetryChartInstance) {
    telemetryChartInstance.resize()
  }
}

async function updateTelemetryChart() {
  if (!hasTelemetryData.value) {
    if (telemetryChartInstance) telemetryChartInstance.clear()
    return
  }

  await initTelemetryChart()
  if (!telemetryChartInstance) return

  const points = telemetryPoints.value
  const xData = points.map((p) => Number(p.ts) || Date.now())
  const cpuData = points.map((p) => toPct(p.cpuPct))
  const memData = points.map((p) => toPct(p.memoryPct))
  const storageData = points.map((p) => toPct(p.storagePct))

  telemetryChartInstance.setOption({
    animation: false,
    grid: { left: 44, right: 14, top: 16, bottom: 28 },
    tooltip: {
      trigger: 'axis',
      backgroundColor: 'rgba(17, 24, 39, 0.92)',
      borderColor: 'rgba(148, 163, 184, 0.35)',
      borderWidth: 1,
      textStyle: { color: '#e5e7eb' },
      valueFormatter: (v) => `${Number(v).toFixed(1)}%`,
      axisPointer: { type: 'line', lineStyle: { color: 'rgba(148, 163, 184, 0.6)', width: 1 } },
    },
    legend: { show: false },
    xAxis: {
      type: 'category',
      boundaryGap: false,
      data: xData,
      axisLine: { lineStyle: { color: 'rgba(148, 163, 184, 0.35)' } },
      axisTick: { show: false },
      axisLabel: {
        color: 'rgba(203, 213, 225, 0.86)',
        fontSize: 11,
        formatter: (v) => formatTelemetryAxisTime(v),
      },
      splitLine: { show: false },
    },
    yAxis: {
      type: 'value',
      min: 0,
      max: 100,
      interval: 20,
      axisLine: { show: true, lineStyle: { color: 'rgba(148, 163, 184, 0.35)' } },
      axisTick: { show: false },
      axisLabel: {
        color: 'rgba(203, 213, 225, 0.86)',
        fontSize: 11,
        formatter: '{value}%',
      },
      splitLine: {
        show: true,
        lineStyle: { color: 'rgba(148, 163, 184, 0.16)', width: 1 },
      },
    },
    series: [
      {
        name: 'CPU',
        type: 'line',
        data: cpuData,
        showSymbol: false,
        smooth: 0.22,
        lineStyle: { width: 1.25, color: '#f87171' },
      },
      {
        name: 'MEM',
        type: 'line',
        data: memData,
        showSymbol: false,
        smooth: 0.22,
        lineStyle: { width: 1.25, color: '#60a5fa' },
      },
      {
        name: 'DISK',
        type: 'line',
        data: storageData,
        showSymbol: false,
        smooth: 0.22,
        lineStyle: { width: 1.25, color: '#34d399' },
      },
    ],
  }, true)
}

onMounted(async () => {
  try {
    await calibrateServerTime()
    await loadStreamInfo()
    await refreshHistoryData(true)
    listenCameraStatus()

    refreshTimer = setInterval(loadStreamInfo, 1000)
    latencyTimer = setInterval(updateLatencyMetrics, 200)
    historyRefreshTimer = setInterval(() => refreshHistoryData(false), 10000)
    window.addEventListener('resize', resizeTelemetryChart)
    await nextTick()
    await updateTelemetryChart()
  } catch (err) {
    error.value = err.message || 'Failed to load stream info'
    connectionState.value = 'error'
  }
})

onBeforeUnmount(() => {
  cleanup()
})

watch(telemetryPoints, () => {
  nextTick().then(() => updateTelemetryChart()).catch(() => {})
}, { deep: true })

watch(timelineZoomPct, () => {
  if (!historyOverview.value?.hasHistory) return
  if (timelineZoomTimer) {
    clearTimeout(timelineZoomTimer)
  }
  timelineZoomTimer = setTimeout(() => {
    timelineZoomTimer = null
    const anchorMs = timelineZoomAnchorMs.value ?? selectedTimelineMs.value
    timelineZoomAnchorMs.value = null
    void loadTimelineWindow(anchorMs, true)
  }, 120)
})

watch(isLiveMode, (liveMode) => {
  if (!historyOverview.value?.hasHistory || !historyOverview.value.timeRange) return
  const center = liveMode
    ? historyOverview.value.timeRange.endMs
    : selectedTimelineMs.value
  void loadTimelineWindow(center, true)
})

async function calibrateServerTime() {
  try {
    await fetch('/api/health')
  } catch {
  }
}

function cleanup() {
  void stopHistoryReplaySession()
  stopLivePlayer()
  clearHistoryVideo()
  if (timelineZoomTimer) {
    clearTimeout(timelineZoomTimer)
    timelineZoomTimer = null
  }
  window.removeEventListener('resize', resizeTelemetryChart)
  if (telemetryChartInstance) {
    telemetryChartInstance.dispose()
    telemetryChartInstance = null
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
  if (historyRefreshTimer) {
    clearInterval(historyRefreshTimer)
    historyRefreshTimer = null
  }
}

function stopLivePlayer() {
  if (player) {
    player.pause()
    player.unload()
    player.detachMediaElement()
    player.destroy()
    player = null
  }
}

function clearHistoryVideo() {
  const video = videoRef.value
  if (!video) return
  video.pause()
  video.removeAttribute('src')
  video.load()
}

async function loadStreamInfo() {
  try {
    const data = await cameraApi.getStreamInfo(cameraId)
    streamInfo.value = data
    if (data.stream_key) {
      liveFlvUrl.value = `${window.location.origin}/live/${data.stream_key}.flv`
    }
    if (isLiveMode.value && liveFlvUrl.value && !player) {
      startFlvPlayer(liveFlvUrl.value)
    }
    if (isLiveMode.value && historyOverview.value?.timeRange?.endMs != null) {
      historySelectedMs.value = Math.min(Date.now(), historyOverview.value.timeRange.endMs)
    }
  } catch (err) {
    console.error('Failed to load stream info:', err)
  }
}

async function refreshHistoryData(forceTimeline) {
  try {
    const overview = await cameraApi.getHistoryOverview(cameraId)
    historyOverview.value = overview
    if (!overview.hasHistory || !overview.timeRange) {
      historyTimeline.value = {
        startMs: null,
        endMs: null,
        ranges: [],
        thumbnails: [],
        segments: [],
        nowMs: Date.now(),
      }
      historySelectedMs.value = Date.now()
      return
    }

    const centerMs = isLiveMode.value
      ? overview.timeRange.endMs
      : (timelineDragMs.value ?? historySelectedMs.value ?? overview.timeRange.endMs)

    await loadTimelineWindow(centerMs, forceTimeline)

    if (historySelectedMs.value == null || isLiveMode.value) {
      historySelectedMs.value = Math.min(nowMs.value, historyTimeline.value.endMs || overview.timeRange.endMs)
    }
  } catch (err) {
    console.error('Failed to refresh history:', err)
  }
}

function startFlvPlayer(url) {
  if (!mpegts.isSupported()) {
    error.value = 'Your browser does not support HTTP-FLV playback'
    connectionState.value = 'error'
    return
  }

  stopLivePlayer()
  clearHistoryVideo()

  const video = videoRef.value
  if (!video) return
  video.controls = false
  video.muted = true

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
    liveBufferLatencyMaxLatency: 0.5,
    liveBufferLatencyMinRemain: 0.1,
    liveBufferLatencyChasingOnPaused: true,
  })

  player.attachMediaElement(video)

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

async function startHistoryPlayback(playbackUrl, offsetSec) {
  stopLivePlayer()

  const video = videoRef.value
  if (!video) return

  const url = playbackUrl.startsWith('http')
    ? playbackUrl
    : `${window.location.origin}${playbackUrl}`

  video.controls = true
  video.muted = false

  const seekSec = Math.max(0, Number(offsetSec) || 0)
  const onLoadedMeta = () => {
    const duration = Number(video.duration)
    if (Number.isFinite(duration) && duration > 0) {
      video.currentTime = Math.min(seekSec, Math.max(0, duration - 0.2))
    } else {
      video.currentTime = seekSec
    }
    video.play().catch(() => {})
    video.removeEventListener('loadedmetadata', onLoadedMeta)
  }

  video.addEventListener('loadedmetadata', onLoadedMeta)
  video.src = url
  video.load()
  connectionState.value = 'connected'
}

async function stopHistoryReplaySession() {
  if (!historyReplaySessionId.value) {
    return
  }
  const sessionId = historyReplaySessionId.value
  historyReplaySessionId.value = null
  historyReplayTransport.value = ''
  try {
    await cameraApi.stopHistoryReplay(cameraId, sessionId)
  } catch {
  }
}

async function seekHistoryAt(tsMs) {
  if (!hasHistory.value || !Number.isFinite(tsMs)) return
  historyLoading.value = true
  historyError.value = ''

  try {
    await stopHistoryReplaySession()
    const playback = await cameraApi.getHistoryPlayback(cameraId, tsMs)
    if (playback.mode !== 'history' || !playback.playbackUrl) {
      historyError.value = 'No recording found at this time'
      return
    }
    playbackMode.value = 'history'
    historySelectedMs.value = tsMs
    historyReplaySessionId.value = playback.sessionId || null
    historyReplayTransport.value = playback.transport || ''
    if ((playback.transport || '').startsWith('flv')) {
      const url = playback.playbackUrl.startsWith('http')
        ? playback.playbackUrl
        : `${window.location.origin}${playback.playbackUrl}`
      startFlvPlayer(url)
    } else {
      await startHistoryPlayback(playback.playbackUrl, playback.offsetSec)
    }
  } catch (err) {
    historyError.value = err.message || 'Failed to load history playback'
  } finally {
    historyLoading.value = false
  }
}

function clampTimelineTs(ts, startMs, endMs) {
  if (!Number.isFinite(ts)) return endMs
  if (!Number.isFinite(startMs) || !Number.isFinite(endMs)) return ts
  return Math.max(startMs, Math.min(endMs, ts))
}

function computeTimelineWindowRange(centerMs, rangeStartMs, rangeEndMs) {
  const safeStart = Number(rangeStartMs)
  const safeEnd = Number(rangeEndMs)
  if (!Number.isFinite(safeStart) || !Number.isFinite(safeEnd) || safeEnd <= safeStart) {
    return { startMs: safeStart, endMs: safeEnd }
  }

  const fullSpan = safeEnd - safeStart
  const windowMs = Math.max(60 * 1000, Math.min(fullSpan, timelineWindowMs.value))
  const safeCenter = clampTimelineTs(centerMs, safeStart, safeEnd)
  let startMs = Math.round(safeCenter - windowMs / 2)
  let endMs = startMs + windowMs

  if (startMs < safeStart) {
    startMs = safeStart
    endMs = Math.min(safeEnd, safeStart + windowMs)
  }
  if (endMs > safeEnd) {
    endMs = safeEnd
    startMs = Math.max(safeStart, safeEnd - windowMs)
  }
  if (endMs <= startMs) {
    startMs = safeStart
    endMs = safeEnd
  }
  return { startMs, endMs }
}

async function loadTimelineWindow(centerMs, force = false) {
  const overview = historyOverview.value
  if (!overview?.hasHistory || !overview.timeRange) return

  const { startMs, endMs } = computeTimelineWindowRange(
    centerMs,
    overview.timeRange.startMs,
    overview.timeRange.endMs
  )

  const currentStart = Number(historyTimeline.value.startMs)
  const currentEnd = Number(historyTimeline.value.endMs)
  if (!force &&
      Number.isFinite(currentStart) &&
      Number.isFinite(currentEnd) &&
      Math.abs(currentStart - startMs) < 1000 &&
      Math.abs(currentEnd - endMs) < 1000) {
    return
  }

  const timeline = await cameraApi.getHistoryTimeline(cameraId, { start: startMs, end: endMs })
  historyTimeline.value = timeline
}

function adjustTimelineZoom(deltaPct, anchorMs = null) {
  if (anchorMs != null && Number.isFinite(Number(anchorMs))) {
    timelineZoomAnchorMs.value = Number(anchorMs)
  }
  const next = Math.max(0, Math.min(100, Number(timelineZoomPct.value) + Number(deltaPct)))
  timelineZoomPct.value = next
}

function onTimelineWheel(event) {
  if (!hasHistory.value) return
  const rect = event.currentTarget?.getBoundingClientRect?.()
  let anchorMs = selectedTimelineMs.value
  if (rect && Number.isFinite(rect.width) && rect.width > 0) {
    const x = Math.max(0, Math.min(rect.width, event.clientX - rect.left))
    const ratio = x / rect.width
    anchorMs = timelineMinMs.value + ratio * Math.max(1, timelineMaxMs.value - timelineMinMs.value)
  }
  const step = event.deltaY < 0 ? 5 : -5
  adjustTimelineZoom(step, anchorMs)
}

function returnToLive() {
  void stopHistoryReplaySession()
  playbackMode.value = 'live'
  historyError.value = ''
  timelineDragMs.value = null
  if (liveFlvUrl.value) {
    startFlvPlayer(liveFlvUrl.value)
  }
}

function onTimelineInput(event) {
  const tsMs = Number(event.target.value)
  if (!Number.isFinite(tsMs)) return
  timelineDragMs.value = tsMs
}

function onTimelineChange(event) {
  const tsMs = Number(event.target.value)
  timelineDragMs.value = null
  historySelectedMs.value = tsMs
  void loadTimelineWindow(tsMs)
  seekHistoryAt(tsMs)
}

function onTimelineThumbError(event) {
  const img = event?.target
  if (!img) return
  const src = img.currentSrc || img.src
  if (src) {
    blockedThumbUrls.value = {
      ...blockedThumbUrls.value,
      [src]: true,
    }
  }
  img.style.display = 'none'
}

function onTimelineThumbLoad(event) {
  const img = event?.target
  if (!img || !img.naturalWidth || !img.naturalHeight) return
  const src = img.currentSrc || img.src
  if (!src) return

  if (normalThumbUrlCache.has(src)) return
  if (blackThumbUrlCache.has(src)) {
    onTimelineThumbError(event)
    return
  }

  const canvas = document.createElement('canvas')
  canvas.width = 8
  canvas.height = 8
  const ctx = canvas.getContext('2d', { willReadFrequently: true })
  if (!ctx) return
  ctx.drawImage(img, 0, 0, 8, 8)
  const { data } = ctx.getImageData(0, 0, 8, 8)
  let sum = 0
  let count = 0
  for (let i = 0; i < data.length; i += 4) {
    const alpha = data[i + 3]
    if (alpha < 16) continue
    const r = data[i]
    const g = data[i + 1]
    const b = data[i + 2]
    const lum = 0.2126 * r + 0.7152 * g + 0.0722 * b
    sum += lum
    count += 1
  }
  const avg = count ? (sum / count) : 255
  if (avg < 18) {
    blackThumbUrlCache.add(src)
    onTimelineThumbError(event)
    return
  }
  normalThumbUrlCache.add(src)
}

function updateLatencyMetrics() {
  nowMs.value = Date.now()
  const video = videoRef.value
  if (!video) return

  const info = { ...latencyInfo.value }

  if (isLiveMode.value && player) {
    if (video.buffered.length > 0) {
      const bufferEnd = video.buffered.end(video.buffered.length - 1)
      info.buffer = Math.max(0, bufferEnd - video.currentTime)
    }

    if (streamInfo.value?.srs) {
      const srs = streamInfo.value.srs
      const serverNow = Date.now()
      if (srs.lastFrameTime) {
        const lastFrameTs = srs.lastFrameTime > 1e12
          ? srs.lastFrameTime
          : srs.lastFrameTime * 1000
        const frameAge = Math.max(0, (serverNow - lastFrameTs) / 1000)
        info.e2e = info.buffer + frameAge
      } else if (srs.publishTime) {
        info.e2e = info.buffer + (serverNow - srs.publishTime) / 1000
      }
    }

    if (player.statisticsInfo) {
      info.speed = Math.round(player.statisticsInfo.speed || 0)
    }
  } else {
    info.buffer = 0
    info.e2e = null
    info.speed = 0
  }

  if (video.getVideoPlaybackQuality) {
    const quality = video.getVideoPlaybackQuality()
    info.dropped = quality.droppedVideoFrames || 0
    info.decoded = quality.totalVideoFrames || 0
  }

  latencyInfo.value = info
}

function listenCameraStatus() {
  socket = connectSignaling(auth.token)
  socket.emit('join-room', { room: `camera-${cameraId}` })
  socket.on('camera-status', ({ cameraId: id, status }) => {
    if (id == cameraId && status === 'offline' && isLiveMode.value) {
      connectionState.value = 'ended'
    }
  })
}

function goBack() {
  router.push('/')
}

function latencyClass(val) {
  if (val == null) return ''
  if (val < 0.5) return 'latency-good'
  if (val < 1.5) return 'latency-warn'
  return 'latency-bad'
}

function formatDuration(ms) {
  const n = Number(ms)
  if (!Number.isFinite(n) || n <= 0) return '0:00'
  const seconds = Math.floor(n / 1000)
  const h = Math.floor(seconds / 3600)
  const m = Math.floor((seconds % 3600) / 60)
  const s = seconds % 60
  if (h > 0) {
    return `${h}:${String(m).padStart(2, '0')}:${String(s).padStart(2, '0')}`
  }
  return `${m}:${String(s).padStart(2, '0')}`
}

function formatDateTime(ts) {
  const n = Number(ts)
  if (!Number.isFinite(n)) return '-'
  return new Date(n).toLocaleString()
}

function formatOptionalNumber(value, precision = 1) {
  const n = Number(value)
  if (!Number.isFinite(n)) return '-'
  return n.toFixed(precision).replace(/\.0$/, '')
}

function formatBitrate(value) {
  const n = Number(value)
  if (!Number.isFinite(n) || n <= 0) return '-'
  if (n >= 1000000) return `${(n / 1000000).toFixed(1)} Mbps`
  return `${Math.round(n / 1000)} Kbps`
}

function formatConfigValue(value) {
  if (Array.isArray(value)) {
    if (!value.length) return '-'
    if (typeof value[0] === 'object') {
      return value
        .map((item) => {
          if (item.width && item.height) return `${item.width}x${item.height}`
          return JSON.stringify(item)
        })
        .join(', ')
    }
    return value.join(', ')
  }
  if (value && typeof value === 'object') {
    return Object.entries(value)
      .map(([k, v]) => `${k}: ${v}`)
      .join(', ')
  }
  if (value === undefined || value === null || value === '') return '-'
  return String(value)
}

function formatUpdatedTime(ts) {
  if (!ts) return '-'
  return new Date(ts).toLocaleTimeString()
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
        <span :class="['mode-badge', isLiveMode ? 'mode-live' : 'mode-history']">{{ isLiveMode ? 'LIVE' : 'HISTORY' }}</span>
      </div>
      <div class="watch-actions">
        <button v-if="isHistoryMode" class="btn btn-primary btn-sm" @click="returnToLive">Back To Live</button>
      </div>
    </div>

    <div class="video-container">
      <video
        ref="videoRef"
        autoplay
        playsinline
        :muted="isLiveMode"
        :controls="isHistoryMode"
        class="video-player"
      ></video>
      <div v-if="connectionState === 'connecting'" class="video-overlay">
        <p>Connecting to stream...</p>
      </div>
      <div v-if="connectionState === 'error'" class="video-overlay video-overlay-error">
        <p>{{ error || 'Connection failed' }}</p>
        <button class="btn btn-primary btn-sm" @click="goBack">Return to Dashboard</button>
      </div>
      <div v-if="connectionState === 'ended' && isLiveMode" class="video-overlay">
        <p>Stream has ended</p>
        <button class="btn btn-primary btn-sm" @click="goBack">Return to Dashboard</button>
      </div>

      <div v-if="streamInfo?.srs && connectionState === 'connected' && isLiveMode" class="stream-bar">
        <span class="bar-item bar-latency" :class="latencyClass(latencyInfo.buffer)">
          Buf: {{ latencyInfo.buffer.toFixed(2) }}s
        </span>
        <span class="bar-sep"></span>
        <span class="bar-item" :class="latencyClass(latencyInfo.e2e)">
          E2E: {{ latencyInfo.e2e ? latencyInfo.e2e.toFixed(2) + 's' : '-' }}
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

    <div class="watch-hint">Scroll down for timeline, SEI chart, and camera config cards.</div>

    <div class="timeline-panel">
      <div class="timeline-header">
        <div>
          <h3>Timeline</h3>
          <p class="timeline-sub">{{ historyStatusLabel }} | {{ timelineLabel }}</p>
        </div>
        <div class="timeline-zoom">
          <span class="timeline-zoom-label">Window {{ timelineWindowLabel }}</span>
          <button class="btn btn-secondary btn-sm" type="button" :disabled="!hasHistory" @click="adjustTimelineZoom(-10)">-</button>
          <input
            v-model.number="timelineZoomPct"
            class="timeline-zoom-slider"
            type="range"
            min="0"
            max="100"
            step="1"
            :disabled="!hasHistory"
          />
          <button class="btn btn-secondary btn-sm" type="button" :disabled="!hasHistory" @click="adjustTimelineZoom(10)">+</button>
        </div>
        <div class="timeline-actions">
          <button class="btn btn-secondary btn-sm" :disabled="!hasHistory || historyLoading" @click="seekHistoryAt(selectedTimelineMs)">
            View At Cursor
          </button>
          <button class="btn btn-primary btn-sm" :disabled="isLiveMode" @click="returnToLive">
            Back To Live
          </button>
        </div>
      </div>

      <template v-if="hasHistory">
        <div class="timeline-track-wrap" title="滚轮缩放时间轴" @wheel.prevent="onTimelineWheel">
          <div class="timeline-filmstrip">
            <div
              v-for="tile in timelineThumbTiles"
              :key="tile.id"
              class="timeline-thumb-tile"
              :class="{ 'is-empty': !tile.thumbnailUrl || blockedThumbUrls[tile.thumbnailUrl] }"
            >
              <img
                v-if="tile.thumbnailUrl"
                :src="tile.thumbnailUrl"
                alt=""
                loading="lazy"
                decoding="async"
                @load="onTimelineThumbLoad"
                @error="onTimelineThumbError"
              />
            </div>
            <div class="timeline-cursor" :style="{ left: `${timelineCursorLeft}%` }"></div>
          </div>
          <input
            class="timeline-slider"
            type="range"
            :min="timelineMinMs"
            :max="timelineMaxMs"
            :step="timelineSliderStepMs"
            :value="selectedTimelineMs"
            @input="onTimelineInput"
            @change="onTimelineChange"
          />
        </div>

        <div class="timeline-axis">
          <span
            v-for="mark in timelineTickMarks"
            :key="mark.id"
            class="axis-tick"
            :class="{ 'axis-tick-major': mark.major }"
            :style="{ left: `${mark.left}%` }"
          >
            <span class="axis-line"></span>
            <span v-if="mark.label" class="axis-label">{{ mark.label }}</span>
          </span>
        </div>

        <div class="timeline-meta">
          <span>Total: {{ formatDuration(displayTotalDurationMs) }}</span>
          <span>Segments: {{ historyOverview?.segmentCount || 0 }}</span>
          <span>Recording: {{ historyOverview?.hasActiveRecording ? 'ON' : 'OFF' }}</span>
          <span>Range: {{ formatDateTime(historyOverview?.timeRange?.startMs) }} ~ {{ formatDateTime(timelineMaxMs) }}</span>
        </div>
      </template>

      <div v-else class="watch-empty">
        No history recordings found for this camera.
      </div>

      <div v-if="historyError" class="error-msg timeline-error">{{ historyError }}</div>
    </div>

    <div class="watch-panels">
      <div class="watch-card">
        <div class="watch-card-header">
          <h3>Device Resource Usage (SEI)</h3>
          <span class="watch-card-meta">{{ telemetryStateLabel }} | Updated {{ formatUpdatedTime(telemetryUpdatedAt) }}</span>
        </div>
        <div v-if="hasTelemetryData" class="telemetry-chart-wrap">
          <div ref="telemetryChartRef" class="telemetry-chart"></div>
          <div class="telemetry-legend">
            <span class="legend-item"><i class="legend-dot legend-cpu"></i>CPU {{ formatOptionalNumber(telemetryInfo?.cpuPct, 1) }}%</span>
            <span class="legend-item"><i class="legend-dot legend-memory"></i>MEM {{ formatOptionalNumber(telemetryInfo?.memoryPct, 1) }}%</span>
            <span class="legend-item"><i class="legend-dot legend-storage"></i>DISK {{ formatOptionalNumber(telemetryInfo?.storagePct, 1) }}%</span>
          </div>
          <div class="telemetry-extra">
            <span>Memory {{ formatOptionalNumber(telemetryInfo?.memoryUsedMb, 1) }} / {{ formatOptionalNumber(telemetryInfo?.memoryTotalMb, 1) }} MB</span>
            <span>Storage {{ formatOptionalNumber(telemetryInfo?.storageUsedGb, 2) }} / {{ formatOptionalNumber(telemetryInfo?.storageTotalGb, 2) }} GB</span>
          </div>
        </div>
        <div v-else class="watch-empty">
          Waiting for SEI telemetry...
        </div>
      </div>

      <div class="watch-card">
        <div class="watch-card-header">
          <h3>Camera Config (SEI)</h3>
        </div>
        <div v-if="cameraConfigItems.length" class="config-grid">
          <div v-for="item in cameraConfigItems" :key="item.key" class="config-item">
            <span class="config-key">{{ item.key }}</span>
            <span class="config-value">{{ item.value }}</span>
          </div>
        </div>
        <div v-else class="watch-empty">
          Waiting for camera config SEI...
        </div>

        <div class="watch-subtitle">Configurable Options</div>
        <div v-if="configurableItems.length" class="config-grid">
          <div v-for="item in configurableItems" :key="item.key" class="config-item">
            <span class="config-key">{{ item.key }}</span>
            <span class="config-value">{{ item.value }}</span>
          </div>
        </div>
        <div v-else class="watch-empty">
          Waiting for configurable options SEI...
        </div>
      </div>
    </div>
  </div>
</template>

<style scoped>
.watch-view {
  max-width: 1100px;
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
  flex-wrap: wrap;
}

.watch-info h2 {
  font-size: 1.2rem;
  font-weight: 600;
}

.watch-actions {
  margin-left: auto;
}

.mode-badge {
  font-size: 0.72rem;
  border-radius: 10px;
  padding: 2px 8px;
  font-weight: 700;
  letter-spacing: 0.3px;
}

.mode-live {
  color: #34d399;
  background: rgba(52, 211, 153, 0.15);
}

.mode-history {
  color: #60a5fa;
  background: rgba(96, 165, 250, 0.15);
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
  width: 100%;
  max-width: min(1100px, max(360px, calc((100vh - 220px) * 16 / 9)));
  margin: 0 auto;
  background: #000;
  border-radius: var(--radius-lg);
  overflow: hidden;
  aspect-ratio: 16 / 9;
}

.watch-hint {
  margin-top: 10px;
  color: var(--text-secondary);
  font-size: 0.8rem;
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

.timeline-panel {
  margin-top: 16px;
  background: var(--bg-card);
  border: 1px solid var(--border-color);
  border-radius: var(--radius-lg);
  padding: 14px 16px;
}

.timeline-header {
  display: flex;
  align-items: center;
  gap: 12px;
  flex-wrap: wrap;
}

.timeline-header h3 {
  font-size: 0.95rem;
  font-weight: 600;
}

.timeline-sub {
  margin-top: 2px;
  color: var(--text-secondary);
  font-size: 0.8rem;
}

.timeline-zoom {
  display: inline-flex;
  align-items: center;
  gap: 8px;
}

.timeline-zoom-label {
  color: var(--text-secondary);
  font-size: 0.78rem;
  min-width: 88px;
}

.timeline-zoom-slider {
  width: 160px;
}

.timeline-actions {
  display: flex;
  gap: 8px;
  margin-left: auto;
}

.timeline-track-wrap {
  margin-top: 12px;
  position: relative;
}

.timeline-filmstrip {
  position: relative;
  display: flex;
  height: 45px;
  border-radius: 10px;
  background: rgba(255, 255, 255, 0.06);
  overflow: hidden;
  border: 1px solid rgba(255, 255, 255, 0.18);
}

.timeline-thumb-tile {
  position: relative;
  flex: 1 1 0;
  min-width: 0;
  background-color: rgba(96, 165, 250, 0.2);
  border-right: 1px solid rgba(255, 255, 255, 0.18);
}

.timeline-thumb-tile img {
  width: 100%;
  height: 100%;
  display: block;
  object-fit: cover;
  object-position: center;
}

.timeline-thumb-tile.is-empty {
  background-image: linear-gradient(135deg, rgba(96, 165, 250, 0.28), rgba(30, 41, 59, 0.22));
}

.timeline-thumb-tile:last-child {
  border-right: none;
}

.timeline-thumb-tile::after {
  content: '';
  position: absolute;
  inset: 0;
  background: linear-gradient(to bottom, rgba(15, 17, 23, 0.03), rgba(15, 17, 23, 0.2));
  pointer-events: none;
}

.timeline-cursor {
  position: absolute;
  top: 0;
  bottom: 0;
  width: 2px;
  transform: translateX(-1px);
  background: rgba(255, 255, 255, 0.92);
  box-shadow: 0 0 0 2px rgba(59, 130, 246, 0.35);
  z-index: 3;
}

.timeline-slider {
  margin-top: 8px;
  width: 100%;
  -webkit-appearance: none;
  appearance: none;
  height: 16px;
  background: transparent;
}

.timeline-slider::-webkit-slider-runnable-track {
  height: 4px;
  border-radius: 999px;
  background: rgba(255, 255, 255, 0.12);
}

.timeline-slider::-webkit-slider-thumb {
  -webkit-appearance: none;
  appearance: none;
  width: 14px;
  height: 14px;
  border-radius: 50%;
  background: var(--accent);
  border: 2px solid #fff;
  cursor: pointer;
  margin-top: -5px;
}

.timeline-slider::-moz-range-track {
  height: 4px;
  border-radius: 999px;
  background: rgba(255, 255, 255, 0.12);
}

.timeline-slider::-moz-range-thumb {
  width: 14px;
  height: 14px;
  border-radius: 50%;
  background: var(--accent);
  border: 2px solid #fff;
  cursor: pointer;
}

.timeline-axis {
  margin-top: 4px;
  position: relative;
  height: 28px;
  color: var(--text-secondary);
  font-size: 0.72rem;
}

.axis-tick {
  position: absolute;
  top: 0;
  transform: translateX(-50%);
}

.axis-line {
  display: block;
  width: 1px;
  height: 5px;
  background: rgba(255, 255, 255, 0.25);
  margin: 0 auto;
}

.axis-tick-major .axis-line {
  height: 8px;
  background: rgba(255, 255, 255, 0.45);
}

.axis-label {
  display: block;
  margin-top: 2px;
  white-space: nowrap;
}

.timeline-meta {
  margin-top: 10px;
  display: flex;
  flex-wrap: wrap;
  gap: 14px;
  color: var(--text-secondary);
  font-size: 0.8rem;
}

.timeline-error {
  margin-top: 8px;
}

.watch-panels {
  margin-top: 16px;
  display: grid;
  grid-template-columns: 1fr;
  gap: 16px;
}

.watch-card {
  background: var(--bg-card);
  border: 1px solid var(--border-color);
  border-radius: var(--radius-lg);
  padding: 14px 16px;
}

.watch-card-header {
  display: flex;
  align-items: baseline;
  justify-content: space-between;
  gap: 12px;
  margin-bottom: 12px;
}

.watch-card-header h3 {
  font-size: 0.95rem;
  font-weight: 600;
}

.watch-card-meta {
  color: var(--text-secondary);
  font-size: 0.78rem;
}

.telemetry-chart-wrap {
  display: grid;
  gap: 10px;
}

.telemetry-chart {
  width: 100%;
  height: 190px;
  background: linear-gradient(180deg, rgba(255, 255, 255, 0.015), rgba(0, 0, 0, 0.14));
  border: 1px solid var(--border-color);
  border-radius: 8px;
}

.telemetry-legend {
  display: flex;
  flex-wrap: wrap;
  gap: 14px;
  font-size: 0.82rem;
}

.legend-item {
  display: inline-flex;
  align-items: center;
  gap: 6px;
  color: var(--text-secondary);
}

.legend-dot {
  width: 8px;
  height: 8px;
  border-radius: 50%;
  display: inline-block;
}

.legend-cpu {
  background: #f87171;
}

.legend-memory {
  background: #60a5fa;
}

.legend-storage {
  background: #34d399;
}

.telemetry-extra {
  display: flex;
  flex-wrap: wrap;
  gap: 16px;
  color: var(--text-secondary);
  font-size: 0.8rem;
}

.watch-subtitle {
  margin: 12px 0 8px;
  font-size: 0.84rem;
  color: var(--text-secondary);
}

.config-grid {
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(220px, 1fr));
  gap: 8px;
}

.config-item {
  background: rgba(255, 255, 255, 0.02);
  border: 1px solid var(--border-color);
  border-radius: 8px;
  padding: 8px 10px;
}

.config-key {
  display: block;
  color: var(--text-secondary);
  font-size: 0.76rem;
  margin-bottom: 4px;
}

.config-value {
  display: block;
  color: var(--text-primary);
  font-size: 0.82rem;
  word-break: break-word;
}

.watch-empty {
  color: var(--text-secondary);
  font-size: 0.84rem;
  padding: 8px 0;
}

@media (max-width: 768px) {
  .video-container {
    max-width: 100%;
  }

  .watch-header {
    flex-wrap: wrap;
  }

  .watch-actions {
    width: 100%;
    margin-left: 0;
  }

  .timeline-header {
    flex-direction: column;
    align-items: flex-start;
  }

  .timeline-zoom {
    width: 100%;
  }

  .timeline-zoom-label {
    min-width: 78px;
  }

  .timeline-zoom-slider {
    flex: 1;
    width: auto;
  }

  .timeline-actions {
    width: 100%;
    margin-left: 0;
  }

  .timeline-actions .btn {
    flex: 1;
  }

  .timeline-filmstrip {
    height: 38px;
  }

  .watch-card-header {
    flex-direction: column;
    align-items: flex-start;
  }

  .telemetry-chart {
    height: 150px;
  }
}
</style>
