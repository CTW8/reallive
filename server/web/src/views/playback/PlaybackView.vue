<script setup>
import { computed, onBeforeUnmount, onMounted, ref, watch } from 'vue'
import { useRoute } from 'vue-router'
import mpegts from 'mpegts.js'
import { cameraApi } from '../../api/index.js'
import { useCameraStore } from '../../stores/camera.js'

const cameraStore = useCameraStore()
const route = useRoute()

const videoRef = ref(null)
const loading = ref(false)
const error = ref('')
const cameraSearch = ref('')

const selectedCameraId = ref('')
const selectedDate = ref(new Date())
const calendarMonth = ref(startOfMonth(new Date()))
const autoAdjustedToHistoryDate = ref(false)

const historyOverview = ref(null)
const historyTimeline = ref({
  startMs: null,
  endMs: null,
  ranges: [],
  thumbnails: [],
  segments: [],
  events: [],
})

const selectedTsMs = ref(null)
const sliderTsMs = ref(null)
const playbackRate = ref(1)
const playing = ref(false)
const historyReplaySessionId = ref(null)

let flvPlayer = null

const cameras = computed(() => cameraStore.cameras || [])

const filteredCameras = computed(() => {
  const keyword = cameraSearch.value.trim().toLowerCase()
  if (!keyword) return cameras.value
  return cameras.value.filter((cam) => String(cam.name || '').toLowerCase().includes(keyword))
})

const selectedCamera = computed(() => {
  const id = Number(selectedCameraId.value)
  return cameras.value.find((cam) => Number(cam.id) === id) || null
})

const selectedCountLabel = computed(() => (selectedCamera.value ? '1' : '0'))

const dayRange = computed(() => {
  const start = new Date(selectedDate.value)
  start.setHours(0, 0, 0, 0)
  const end = new Date(start)
  end.setDate(end.getDate() + 1)
  end.setMilliseconds(-1)
  return { startMs: start.getTime(), endMs: end.getTime() }
})

const timelineMinMs = computed(() => {
  if (Number.isFinite(Number(historyTimeline.value.startMs))) return Number(historyTimeline.value.startMs)
  return dayRange.value.startMs
})

const timelineMaxMs = computed(() => {
  if (Number.isFinite(Number(historyTimeline.value.endMs))) return Number(historyTimeline.value.endMs)
  return dayRange.value.endMs
})

const hasHistory = computed(() => {
  const min = timelineMinMs.value
  const max = timelineMaxMs.value
  return Number.isFinite(min) && Number.isFinite(max) && max > min && (historyTimeline.value.segments || []).length > 0
})

const activeTsMs = computed(() => {
  if (sliderTsMs.value != null) return sliderTsMs.value
  if (selectedTsMs.value != null) return selectedTsMs.value
  return timelineMaxMs.value
})

const timelineSpanMs = computed(() => Math.max(1, timelineMaxMs.value - timelineMinMs.value))

const timelineThumbTiles = computed(() => {
  const min = timelineMinMs.value
  const span = timelineSpanMs.value
  const tileCount = 20
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
  const thumbs = (explicitThumbs.length ? explicitThumbs : segmentThumbs).sort((a, b) => a.ts - b.ts)

  if (!thumbs.length) {
    return Array.from({ length: tileCount }, (_, i) => ({ id: `empty-${i}`, ts: null, url: null }))
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
      id: `thumb-${i}`,
      ts: Math.round(centerTs),
      url: thumbs[cursor].url,
    }
  })
})

const timelineSegments = computed(() => {
  const min = timelineMinMs.value
  const span = timelineSpanMs.value
  return (historyTimeline.value.segments || [])
    .map((segment) => {
      const startMs = Number(segment.startMs)
      const endMs = Number(segment.endMs)
      if (!Number.isFinite(startMs) || !Number.isFinite(endMs)) return null
      const left = ((Math.max(min, startMs) - min) / span) * 100
      const width = (Math.max(1, Math.min(endMs, timelineMaxMs.value) - Math.max(min, startMs)) / span) * 100
      return {
        left: Math.max(0, Math.min(100, left)),
        width: Math.max(0.3, Math.min(100, width)),
      }
    })
    .filter(Boolean)
})

const timelineEvents = computed(() => {
  const min = timelineMinMs.value
  const span = timelineSpanMs.value
  return (historyTimeline.value.events || [])
    .map((evt, index) => {
      const ts = Number(evt.ts)
      if (!Number.isFinite(ts)) return null
      if (ts < min || ts > timelineMaxMs.value) return null
      const left = ((ts - min) / span) * 100
      const normalizedType = normalizeEventType(evt.type)
      return {
        ...evt,
        id: `event-${ts}-${index}`,
        ts,
        type: normalizedType,
        left: Math.max(0, Math.min(100, left)),
      }
    })
    .filter(Boolean)
})

const eventRows = computed(() => [...timelineEvents.value].reverse().slice(0, 12))

const timelineCursorLeft = computed(() => {
  const min = timelineMinMs.value
  const span = timelineSpanMs.value
  const left = ((activeTsMs.value - min) / span) * 100
  return Math.max(0, Math.min(100, left))
})

const timelineLabels = computed(() => {
  const count = 7
  return Array.from({ length: count }, (_, i) => {
    const ratio = i / (count - 1)
    const ts = Math.round(timelineMinMs.value + ratio * timelineSpanMs.value)
    return {
      id: `label-${i}`,
      label: formatClock(ts),
    }
  })
})

const currentTimeLabel = computed(() => {
  if (!Number.isFinite(activeTsMs.value)) return '--:--:--'
  return formatClock(activeTsMs.value)
})

const dayEndLabel = computed(() => formatClock(timelineMaxMs.value))

const currentDateTitle = computed(() => {
  return selectedDate.value.toLocaleDateString([], {
    year: 'numeric',
    month: 'long',
    day: 'numeric',
  })
})

const calendarTitle = computed(() => calendarMonth.value.toLocaleDateString([], { year: 'numeric', month: 'long' }))

const calendarCells = computed(() => {
  const first = startOfMonth(calendarMonth.value)
  const startWeekday = first.getDay()
  const start = new Date(first)
  start.setDate(first.getDate() - startWeekday)

  return Array.from({ length: 42 }, (_, i) => {
    const date = new Date(start)
    date.setDate(start.getDate() + i)
    const ts = date.getTime()
    return {
      id: `${date.getFullYear()}-${date.getMonth()}-${date.getDate()}`,
      label: date.getDate(),
      date,
      ts,
      inMonth: date.getMonth() === calendarMonth.value.getMonth() && date.getFullYear() === calendarMonth.value.getFullYear(),
      selected: isSameDate(date, selectedDate.value),
      today: isSameDate(date, new Date()),
    }
  })
})

const timelineStepMs = computed(() => Math.max(1000, Math.floor(timelineSpanMs.value / 400)))

watch(selectedCameraId, async () => {
  autoAdjustedToHistoryDate.value = false
  stopReplayOnly()
  await refreshHistory(true)
})

watch(selectedDate, async (date) => {
  calendarMonth.value = startOfMonth(date)
  autoAdjustedToHistoryDate.value = true
  stopReplayOnly()
  await refreshHistory(true)
})

watch(playbackRate, (rate) => {
  const video = videoRef.value
  if (!video) return
  video.playbackRate = Number(rate) || 1
})

onMounted(async () => {
  await cameraStore.fetchCameras()
  const queryCameraId = Number(route.query?.cameraId || 0)
  const initial = cameras.value.find((cam) => Number(cam.id) === queryCameraId) || cameras.value[0]
  if (initial) {
    selectedCameraId.value = String(initial.id)
    await refreshHistory(true)
  }
})

onBeforeUnmount(() => {
  stopReplayOnly()
  destroyFlvPlayer()
  clearVideoSrc()
})

async function refreshHistory(force = false) {
  if (!selectedCamera.value) {
    historyOverview.value = null
    historyTimeline.value = { startMs: null, endMs: null, ranges: [], thumbnails: [], segments: [], events: [] }
    selectedTsMs.value = null
    return
  }

  loading.value = true
  error.value = ''

  try {
    const overview = await cameraApi.getHistoryOverview(selectedCamera.value.id)
    historyOverview.value = overview

    if (!overview?.hasHistory || !overview?.timeRange) {
      historyTimeline.value = {
        startMs: dayRange.value.startMs,
        endMs: dayRange.value.endMs,
        ranges: [],
        thumbnails: [],
        segments: [],
        events: [],
      }
      selectedTsMs.value = dayRange.value.startMs
      return
    }

    const startMs = Math.max(dayRange.value.startMs, Number(overview.timeRange.startMs))
    const endMs = Math.min(dayRange.value.endMs, Number(overview.timeRange.endMs))

    if (!Number.isFinite(startMs) || !Number.isFinite(endMs) || endMs <= startMs) {
      if (!autoAdjustedToHistoryDate.value) {
        const latest = new Date(Number(overview.timeRange.endMs))
        if (!Number.isNaN(latest.getTime()) && !isSameDate(latest, selectedDate.value)) {
          latest.setHours(0, 0, 0, 0)
          selectedDate.value = latest
          return
        }
      }
      historyTimeline.value = {
        startMs: dayRange.value.startMs,
        endMs: dayRange.value.endMs,
        ranges: [],
        thumbnails: [],
        segments: [],
        events: [],
      }
      selectedTsMs.value = dayRange.value.endMs
      return
    }

    const timeline = await cameraApi.getHistoryTimeline(selectedCamera.value.id, { start: startMs, end: endMs })
    historyTimeline.value = {
      ...timeline,
      startMs,
      endMs,
      events: Array.isArray(timeline?.events) ? timeline.events : [],
      thumbnails: Array.isArray(timeline?.thumbnails) ? timeline.thumbnails : [],
      segments: Array.isArray(timeline?.segments) ? timeline.segments : [],
    }

    if (force || !Number.isFinite(Number(selectedTsMs.value))) {
      selectedTsMs.value = Math.min(endMs, Math.max(startMs, Number(overview.timeRange.endMs)))
    } else {
      selectedTsMs.value = clamp(Number(selectedTsMs.value), startMs, endMs)
    }
  } catch (err) {
    error.value = err?.message || 'Failed to load playback history'
  } finally {
    loading.value = false
  }
}

async function seekHistoryAt(tsMs) {
  if (!selectedCamera.value || !Number.isFinite(tsMs)) return
  if (!hasHistory.value) {
    error.value = 'No recording found in selected range'
    return
  }

  loading.value = true
  error.value = ''
  sliderTsMs.value = null

  try {
    await stopReplayOnly()
    const playback = await cameraApi.getHistoryPlayback(selectedCamera.value.id, tsMs)
    if (playback.mode !== 'history' || !playback.playbackUrl) {
      error.value = 'No recording found at this time'
      return
    }

    historyReplaySessionId.value = playback.sessionId || null
    selectedTsMs.value = tsMs

    if ((playback.transport || '').startsWith('flv')) {
      const url = playback.playbackUrl.startsWith('http')
        ? playback.playbackUrl
        : `${window.location.origin}${playback.playbackUrl}`
      startFlvPlayer(url)
    } else {
      await startFilePlayback(playback.playbackUrl, playback.offsetSec)
    }
  } catch (err) {
    error.value = err?.message || 'Failed to start playback'
  } finally {
    loading.value = false
  }
}

async function stopReplayOnly() {
  if (historyReplaySessionId.value && selectedCamera.value) {
    try {
      await cameraApi.stopHistoryReplay(selectedCamera.value.id, historyReplaySessionId.value)
    } catch {
    }
  }
  historyReplaySessionId.value = null
}

function startFlvPlayer(url) {
  if (!mpegts.isSupported()) {
    error.value = 'Your browser does not support HTTP-FLV playback'
    return
  }

  destroyFlvPlayer()
  clearVideoSrc()

  const video = videoRef.value
  if (!video) return
  video.controls = true
  video.muted = false

  flvPlayer = mpegts.createPlayer({
    type: 'flv',
    isLive: true,
    url,
  }, {
    enableWorker: true,
    enableStashBuffer: false,
    lazyLoad: false,
    autoCleanupSourceBuffer: true,
  })

  flvPlayer.attachMediaElement(video)
  flvPlayer.on(mpegts.Events.ERROR, (_, detail) => {
    error.value = detail ? `Playback error: ${detail}` : 'Playback error'
  })

  flvPlayer.load()
  flvPlayer.play()
  video.playbackRate = Number(playbackRate.value) || 1
  playing.value = true
}

async function startFilePlayback(playbackUrl, offsetSec) {
  destroyFlvPlayer()

  const video = videoRef.value
  if (!video) return

  const url = playbackUrl.startsWith('http')
    ? playbackUrl
    : `${window.location.origin}${playbackUrl}`

  const seekSec = Math.max(0, Number(offsetSec) || 0)

  await new Promise((resolve) => {
    const onLoaded = () => {
      const duration = Number(video.duration)
      if (Number.isFinite(duration) && duration > 0) {
        video.currentTime = Math.min(seekSec, Math.max(0, duration - 0.2))
      } else {
        video.currentTime = seekSec
      }
      video.playbackRate = Number(playbackRate.value) || 1
      video.play().catch(() => {})
      video.removeEventListener('loadedmetadata', onLoaded)
      resolve()
    }

    video.addEventListener('loadedmetadata', onLoaded)
    video.src = url
    video.load()
  })

  playing.value = true
}

function destroyFlvPlayer() {
  if (!flvPlayer) return
  flvPlayer.pause()
  flvPlayer.unload()
  flvPlayer.detachMediaElement()
  flvPlayer.destroy()
  flvPlayer = null
}

function clearVideoSrc() {
  const video = videoRef.value
  if (!video) return
  video.pause()
  video.removeAttribute('src')
  video.load()
  playing.value = false
}

function togglePlayPause() {
  const video = videoRef.value
  if (!video) return

  if (!video.src && selectedTsMs.value != null) {
    void seekHistoryAt(selectedTsMs.value)
    return
  }

  if (video.paused) {
    video.play().catch(() => {})
    playing.value = true
  } else {
    video.pause()
    playing.value = false
  }
}

function jumpBy(deltaMs) {
  const min = timelineMinMs.value
  const max = timelineMaxMs.value
  const next = clamp(Number(activeTsMs.value) + deltaMs, min, max)
  selectedTsMs.value = next
  void seekHistoryAt(next)
}

function jumpToEventByOffset(offset) {
  if (!timelineEvents.value.length) return
  const sorted = [...timelineEvents.value].sort((a, b) => a.ts - b.ts)
  const current = Number(activeTsMs.value)

  let index = sorted.findIndex((evt) => evt.ts >= current)
  if (index === -1) index = sorted.length - 1
  index = clamp(index + offset, 0, sorted.length - 1)
  const target = sorted[index]
  if (!target) return
  selectedTsMs.value = target.ts
  void seekHistoryAt(target.ts)
}

function onSliderInput(event) {
  const value = Number(event.target.value)
  if (!Number.isFinite(value)) return
  sliderTsMs.value = value
}

function onSliderChange(event) {
  const value = Number(event.target.value)
  sliderTsMs.value = null
  if (!Number.isFinite(value)) return
  selectedTsMs.value = value
  void seekHistoryAt(value)
}

function selectCamera(camera) {
  if (!camera) return
  selectedCameraId.value = String(camera.id)
}

function chooseDate(cell) {
  if (!cell?.date) return
  selectedDate.value = new Date(cell.date)
}

function prevMonth() {
  const month = new Date(calendarMonth.value)
  month.setMonth(month.getMonth() - 1)
  calendarMonth.value = startOfMonth(month)
}

function nextMonth() {
  const month = new Date(calendarMonth.value)
  month.setMonth(month.getMonth() + 1)
  calendarMonth.value = startOfMonth(month)
}

function normalizeEventType(type) {
  const normalized = String(type || '').trim().toLowerCase()
  if (normalized === 'person' || normalized === 'person_detected' || normalized === 'person-detected') return 'person-detected'
  if (normalized.includes('motion')) return 'motion'
  return normalized || 'event'
}

function eventTypeLabel(type) {
  if (type === 'person-detected') return 'Person detected'
  if (type === 'motion') return 'Motion detected'
  return type || 'Event'
}

function eventMarkerClass(type) {
  if (type === 'person-detected') return 'tl-event person'
  if (type === 'motion') return 'tl-event motion'
  return 'tl-event alert'
}

function eventRowClass(type) {
  if (type === 'person-detected') return 'pe-dot person'
  if (type === 'motion') return 'pe-dot motion'
  return 'pe-dot alert'
}

function formatClock(ts) {
  if (!Number.isFinite(Number(ts))) return '--:--'
  return new Date(Number(ts)).toLocaleTimeString([], {
    hour: '2-digit',
    minute: '2-digit',
    second: '2-digit',
    hour12: false,
  })
}

function formatEventDuration(evt) {
  const durationMs = Number(evt?.durationMs || evt?.duration || 0)
  if (!Number.isFinite(durationMs) || durationMs <= 0) return '--'
  const sec = Math.floor(durationMs / 1000)
  const min = Math.floor(sec / 60)
  const rem = sec % 60
  return `${min}:${String(rem).padStart(2, '0')}`
}

function clamp(value, min, max) {
  return Math.max(min, Math.min(max, value))
}

function isSameDate(a, b) {
  return a.getFullYear() === b.getFullYear() && a.getMonth() === b.getMonth() && a.getDate() === b.getDate()
}

function startOfMonth(date) {
  const month = new Date(date)
  month.setDate(1)
  month.setHours(0, 0, 0, 0)
  return month
}
</script>

<template>
  <div class="playback-page">
    <div class="pb-layout">
      <div class="pb-sidebar">
        <div class="pbs-header">
          <div class="pbs-title-row">
            <h3>Date & Cameras</h3>
            <span class="pbs-selected"><span class="mi">check_circle</span><strong>{{ selectedCountLabel }}</strong> selected</span>
          </div>
          <p class="pbs-sub">Single camera playback mode.</p>
        </div>

        <div class="pbs-section pbs-date-section">
          <div class="mini-cal">
            <div class="pbs-section-head"><span class="mi">calendar_month</span><span>Date</span></div>
            <div class="mc-nav">
              <button type="button" title="Previous Month" @click="prevMonth"><span class="mi">chevron_left</span></button>
              <span>{{ calendarTitle }}</span>
              <button type="button" title="Next Month" @click="nextMonth"><span class="mi">chevron_right</span></button>
            </div>
            <div class="mc-grid">
              <span class="mc-day">S</span>
              <span class="mc-day">M</span>
              <span class="mc-day">T</span>
              <span class="mc-day">W</span>
              <span class="mc-day">T</span>
              <span class="mc-day">F</span>
              <span class="mc-day">S</span>
              <button
                v-for="cell in calendarCells"
                :key="cell.id"
                type="button"
                class="mc-date"
                :class="{ dim: !cell.inMonth, selected: cell.selected, today: cell.today }"
                @click="chooseDate(cell)"
              >
                {{ cell.label }}
              </button>
            </div>
          </div>
        </div>

        <div class="pbs-section pbs-camera-section">
          <div class="pbs-cam-top">
            <div class="pbs-section-head"><span class="mi">videocam</span><span>Cameras</span></div>
            <span class="pbs-cam-all">Single select</span>
          </div>
          <label class="pbs-cam-search">
            <span class="mi">search</span>
            <input v-model="cameraSearch" type="text" placeholder="Search cameras">
          </label>
          <div class="pbs-cameras">
            <button
              v-for="camera in filteredCameras"
              :key="camera.id"
              type="button"
              class="pbs-cam-item"
              :class="{ selected: String(camera.id) === String(selectedCameraId) }"
              @click="selectCamera(camera)"
            >
              <span class="pci-dot" :class="camera.status || 'offline'"></span>
              <span class="pci-name">{{ camera.name }}</span>
            </button>
          </div>
        </div>
      </div>

      <div class="pb-main">
        <div class="pb-video-area single">
          <div class="pb-video-cell">
            <video ref="videoRef" class="pb-video" playsinline controls @play="playing = true" @pause="playing = false"></video>
            <div v-if="loading" class="pv-overlay">Loading…</div>
            <div v-else-if="!hasHistory" class="pv-overlay">No recording for selected date</div>
            <div class="pv-top">
              <span class="cam-name">{{ selectedCamera?.name || 'No camera selected' }}</span>
              <span class="pb-speed">{{ playbackRate }}x</span>
            </div>
          </div>
        </div>

        <div class="pb-controls">
          <div class="pb-controls-top">
            <div class="pbc-left">
              <select v-model.number="playbackRate" class="speed-select">
                <option :value="0.5">0.5x</option>
                <option :value="1">1x</option>
                <option :value="2">2x</option>
                <option :value="4">4x</option>
              </select>
            </div>
            <div class="pbc-center">
              <button class="pbc-btn" title="Previous Event" @click="jumpToEventByOffset(-1)"><span class="mi">skip_previous</span></button>
              <button class="pbc-btn" title="Step Back" @click="jumpBy(-10000)"><span class="mi">fast_rewind</span></button>
              <button class="pbc-btn play" title="Play / Pause" @click="togglePlayPause"><span class="mi">{{ playing ? 'pause' : 'play_arrow' }}</span></button>
              <button class="pbc-btn" title="Step Forward" @click="jumpBy(10000)"><span class="mi">fast_forward</span></button>
              <button class="pbc-btn" title="Next Event" @click="jumpToEventByOffset(1)"><span class="mi">skip_next</span></button>
            </div>
            <div class="pbc-right">
              <span class="pbc-time">{{ currentTimeLabel }} / {{ dayEndLabel }}</span>
            </div>
          </div>

          <div class="pb-timeline-wrap">
            <div class="pb-filmstrip">
              <div v-for="tile in timelineThumbTiles" :key="tile.id" class="pb-thumb" :class="{ empty: !tile.url }">
                <img v-if="tile.url" :src="tile.url" alt="" loading="lazy" decoding="async">
              </div>
            </div>

            <div class="pb-timeline">
              <span class="pb-tl-start">{{ formatClock(timelineMinMs) }}</span>
              <div class="pb-tl-bar">
                <div
                  v-for="(segment, index) in timelineSegments"
                  :key="`seg-${index}`"
                  class="tl-segment normal"
                  :style="{ left: `${segment.left}%`, width: `${segment.width}%` }"
                ></div>
                <button
                  v-for="event in timelineEvents"
                  :key="event.id"
                  type="button"
                  :class="eventMarkerClass(event.type)"
                  :style="{ left: `${event.left}%` }"
                  :title="`${eventTypeLabel(event.type)} ${formatClock(event.ts)}`"
                  @click="seekHistoryAt(event.ts)"
                ></button>
                <div class="tl-cursor" :style="{ left: `${timelineCursorLeft}%` }"></div>
              </div>
              <span class="pb-tl-end">{{ formatClock(timelineMaxMs) }}</span>
            </div>

            <input
              class="timeline-slider"
              type="range"
              :min="timelineMinMs"
              :max="timelineMaxMs"
              :step="timelineStepMs"
              :value="activeTsMs"
              :disabled="!hasHistory"
              @input="onSliderInput"
              @change="onSliderChange"
            >

            <div class="pb-tl-labels">
              <span v-for="item in timelineLabels" :key="item.id">{{ item.label }}</span>
            </div>

            <div class="chart-legend">
              <div class="legend-item"><span class="legend-dot continuous"></span> Continuous</div>
              <div class="legend-item"><span class="legend-dot motion"></span> Motion</div>
              <div class="legend-item"><span class="legend-dot alert"></span> Alert</div>
            </div>
          </div>
        </div>

        <div class="pb-events">
          <h4>
            Events on {{ currentDateTitle }}
            <span class="event-count">{{ timelineEvents.length }} events</span>
          </h4>
          <div v-if="error" class="pb-error">{{ error }}</div>
          <button
            v-for="event in eventRows"
            :key="event.id"
            type="button"
            class="pb-event-row"
            @click="seekHistoryAt(event.ts)"
          >
            <span class="pe-dot" :class="eventRowClass(event.type)"></span>
            <span class="pe-time">{{ formatClock(event.ts) }}</span>
            <span class="pe-text">{{ eventTypeLabel(event.type) }} - {{ selectedCamera?.name || 'Camera' }}</span>
            <span class="pe-dur">{{ formatEventDuration(event) }}</span>
          </button>
          <div v-if="!eventRows.length" class="pb-events-empty">No event in selected day.</div>
        </div>
      </div>
    </div>
  </div>
</template>

<style scoped>
.playback-page {
  height: calc(100vh - var(--topbar-h) - 48px);
}

.pb-layout {
  display: grid;
  grid-template-columns: 300px 1fr;
  gap: 16px;
  height: 100%;
}

.pb-sidebar {
  background: var(--sc);
  border-radius: var(--r3);
  display: flex;
  flex-direction: column;
  overflow: hidden;
  padding: 12px;
  gap: 10px;
}

.pbs-header {
  padding: 12px;
  border: 1px solid var(--olv);
  border-radius: var(--r2);
  background: var(--sc2);
  flex-shrink: 0;
}

.pbs-title-row {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 8px;
  margin-bottom: 4px;
}

.pbs-header h3 {
  font: 500 15px/22px 'Roboto', sans-serif;
  margin: 0;
}

.pbs-selected {
  display: inline-flex;
  align-items: center;
  gap: 4px;
  padding: 2px 8px;
  border-radius: 999px;
  background: var(--pri-c);
  color: var(--on-pri-c);
  font: 500 11px/16px 'Roboto', sans-serif;
  white-space: nowrap;
}

.pbs-selected .mi {
  font-size: 14px;
}

.pbs-sub {
  font: 400 12px/16px 'Roboto', sans-serif;
  color: var(--on-sfv);
  margin: 0;
}

.pbs-section {
  border: 1px solid var(--olv);
  border-radius: var(--r2);
  background: var(--sc2);
}

.pbs-section-head {
  display: flex;
  align-items: center;
  gap: 5px;
  font: 500 11px/16px 'Roboto', sans-serif;
  color: var(--ol);
  text-transform: uppercase;
  letter-spacing: .5px;
}

.pbs-section-head .mi {
  font-size: 14px;
}

.pbs-date-section {
  padding: 12px;
  flex-shrink: 0;
}

.mini-cal {
  display: flex;
  flex-direction: column;
  gap: 8px;
}

.mc-nav {
  display: flex;
  align-items: center;
  justify-content: space-between;
  font: 500 12px/16px 'Roboto', sans-serif;
}

.mc-nav button {
  border: 1px solid var(--olv);
  border-radius: 8px;
  background: transparent;
  color: var(--on-sfv);
  width: 26px;
  height: 26px;
  display: inline-flex;
  align-items: center;
  justify-content: center;
  cursor: pointer;
}

.mc-grid {
  display: grid;
  grid-template-columns: repeat(7, minmax(0, 1fr));
  gap: 4px;
}

.mc-day {
  text-align: center;
  font: 500 11px/16px 'Roboto', sans-serif;
  color: var(--on-sfv);
}

.mc-date {
  border: 1px solid transparent;
  border-radius: 8px;
  height: 30px;
  background: transparent;
  color: var(--on-sf);
  font: 500 12px/16px 'Roboto', sans-serif;
  cursor: pointer;
}

.mc-date.dim {
  opacity: .4;
}

.mc-date.today {
  border-color: rgba(200,191,255,.35);
}

.mc-date.selected {
  background: var(--pri-c);
  color: var(--on-pri-c);
}

.pbs-camera-section {
  display: flex;
  flex-direction: column;
  min-height: 0;
  padding: 10px;
}

.pbs-cam-top {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 8px;
  padding: 0 2px 8px;
}

.pbs-cam-all {
  height: 24px;
  padding: 0 8px;
  border-radius: var(--r6);
  border: 1px solid var(--olv);
  background: transparent;
  font: 500 11px/16px 'Roboto', sans-serif;
  color: var(--on-sfv);
}

.pbs-cam-search {
  display: flex;
  align-items: center;
  gap: 6px;
  border: 1px solid var(--olv);
  border-radius: var(--r2);
  height: 32px;
  padding: 0 8px;
  margin-bottom: 8px;
}

.pbs-cam-search .mi {
  font-size: 16px;
  color: var(--on-sfv);
}

.pbs-cam-search input {
  flex: 1;
  min-width: 0;
  border: none;
  outline: none;
  background: transparent;
  color: var(--on-sf);
  font: 400 12px/16px 'Roboto', sans-serif;
}

.pbs-cameras {
  overflow: auto;
  display: grid;
  gap: 6px;
}

.pbs-cam-item {
  width: 100%;
  border: 1px solid var(--olv);
  background: transparent;
  border-radius: var(--r2);
  padding: 8px 10px;
  display: flex;
  align-items: center;
  gap: 8px;
  text-align: left;
  color: var(--on-sf);
  cursor: pointer;
}

.pbs-cam-item.selected {
  border-color: rgba(200,191,255,.45);
  background: rgba(200,191,255,.08);
}

.pci-dot {
  width: 8px;
  height: 8px;
  border-radius: 50%;
  background: var(--red);
  flex-shrink: 0;
}

.pci-dot.online,
.pci-dot.streaming {
  background: var(--green);
}

.pci-dot.recording {
  background: var(--orange);
}

.pci-name {
  font: 500 12px/16px 'Roboto', sans-serif;
}

.pb-main {
  min-width: 0;
  display: flex;
  flex-direction: column;
  gap: 12px;
}

.pb-video-area {
  background: var(--sc);
  border-radius: var(--r3);
  padding: 12px;
}

.pb-video-cell {
  border-radius: 10px;
  position: relative;
  background: #0b0f1a;
  aspect-ratio: 16 / 9;
  overflow: hidden;
}

.pb-video {
  width: 100%;
  height: 100%;
  object-fit: contain;
  background: #05070d;
}

.pv-top {
  position: absolute;
  left: 12px;
  right: 12px;
  top: 12px;
  display: flex;
  align-items: center;
  justify-content: space-between;
  z-index: 2;
  pointer-events: none;
}

.cam-name {
  font: 500 12px/16px 'Roboto', sans-serif;
  color: rgba(255,255,255,.95);
  padding: 4px 8px;
  border-radius: 999px;
  background: rgba(0,0,0,.35);
}

.pb-speed {
  font: 500 11px/16px 'Roboto', sans-serif;
  color: rgba(255,255,255,.95);
  border-radius: 999px;
  background: rgba(0,0,0,.35);
  padding: 4px 8px;
}

.pv-overlay {
  position: absolute;
  inset: 0;
  background: rgba(4, 7, 12, .55);
  display: flex;
  align-items: center;
  justify-content: center;
  color: #f8fafc;
  font: 500 13px/18px 'Roboto', sans-serif;
  z-index: 3;
}

.pb-controls {
  background: var(--sc);
  border-radius: var(--r3);
  padding: 14px 18px;
}

.pb-controls-top {
  display: grid;
  grid-template-columns: auto 1fr auto;
  align-items: center;
  gap: 12px;
  margin-bottom: 10px;
}

.pbc-left, .pbc-center, .pbc-right {
  display: flex;
  align-items: center;
  gap: 6px;
}

.pbc-center {
  justify-content: center;
}

.pbc-btn {
  width: 34px;
  height: 34px;
  border-radius: 50%;
  border: none;
  background: transparent;
  color: var(--on-sfv);
  display: flex;
  align-items: center;
  justify-content: center;
  cursor: pointer;
}

.pbc-btn:hover {
  background: var(--sc2);
}

.pbc-btn .mi {
  font-size: 22px;
}

.pbc-btn.play {
  background: var(--pri);
  color: var(--on-pri);
}

.pbc-time {
  font: 500 13px/18px 'Roboto', sans-serif;
  color: var(--on-sfv);
}

.speed-select {
  height: 30px;
  padding: 0 8px;
  border-radius: var(--r2);
  border: 1px solid var(--olv);
  background: var(--sc2);
  color: var(--on-sf);
  font: 500 12px/16px 'Roboto', sans-serif;
  outline: none;
}

.pb-timeline-wrap {
  display: flex;
  flex-direction: column;
  gap: 8px;
}

.pb-filmstrip {
  height: 66px;
  border: 1px solid var(--olv);
  border-radius: 8px;
  overflow: hidden;
  display: grid;
  grid-template-columns: repeat(20, minmax(0, 1fr));
  gap: 1px;
  background: rgba(15, 23, 42, .35);
}

.pb-thumb {
  background: rgba(30, 41, 59, .55);
  min-width: 0;
}

.pb-thumb img {
  width: 100%;
  height: 100%;
  object-fit: cover;
}

.pb-thumb.empty {
  background: linear-gradient(140deg, rgba(51,65,85,.45), rgba(15,23,42,.65));
}

.pb-timeline {
  position: relative;
  height: 34px;
}

.pb-tl-bar {
  position: absolute;
  top: 9px;
  left: 66px;
  right: 66px;
  height: 16px;
  background: var(--sc2);
  border-radius: 3px;
  overflow: hidden;
}

.pb-tl-bar .tl-segment {
  position: absolute;
  top: 0;
  height: 100%;
  border-radius: 2px;
}

.pb-tl-bar .tl-segment.normal {
  background: rgba(200,191,255,.32);
}

.tl-event {
  position: absolute;
  top: 2px;
  width: 3px;
  height: 12px;
  border: none;
  transform: translateX(-50%);
  cursor: pointer;
}

.tl-event.person {
  background: #f97316;
}

.tl-event.motion {
  background: #38bdf8;
}

.tl-event.alert {
  background: #ef4444;
}

.pb-tl-bar .tl-cursor {
  position: absolute;
  top: -2px;
  width: 2px;
  height: 20px;
  background: var(--pri);
  z-index: 2;
  transform: translateX(-50%);
}

.pb-tl-bar .tl-cursor::after {
  content: '';
  position: absolute;
  top: -3px;
  left: -4px;
  width: 10px;
  height: 10px;
  border-radius: 50%;
  background: var(--pri);
}

.pb-tl-start, .pb-tl-end {
  position: absolute;
  top: 9px;
  font: 400 11px/16px 'Roboto', sans-serif;
  color: var(--on-sfv);
}

.pb-tl-start { left: 0; }
.pb-tl-end { right: 0; }

.timeline-slider {
  width: calc(100% - 132px);
  margin-left: 66px;
  accent-color: var(--pri);
}

.pb-tl-labels {
  display: grid;
  grid-template-columns: repeat(7, minmax(0, 1fr));
  color: var(--on-sfv);
  font: 400 11px/16px 'Roboto', sans-serif;
}

.pb-tl-labels span {
  text-align: center;
}

.chart-legend {
  display: flex;
  align-items: center;
  gap: 12px;
  color: var(--on-sfv);
  font: 400 11px/16px 'Roboto', sans-serif;
}

.legend-item {
  display: inline-flex;
  align-items: center;
  gap: 5px;
}

.legend-dot {
  width: 8px;
  height: 8px;
  border-radius: 50%;
}

.legend-dot.continuous { background: rgba(200,191,255,.45); }
.legend-dot.motion { background: rgba(56,189,248,.9); }
.legend-dot.alert { background: rgba(239,68,68,.9); }

.pb-events {
  background: var(--sc);
  border-radius: var(--r3);
  padding: 12px 14px;
  overflow: auto;
}

.pb-events h4 {
  margin: 0 0 8px;
  display: flex;
  align-items: center;
  gap: 8px;
  font: 500 14px/20px 'Roboto', sans-serif;
}

.event-count {
  color: var(--on-sfv);
  font-size: 12px;
}

.pb-error {
  margin-bottom: 8px;
  border: 1px solid rgba(239,68,68,.35);
  background: rgba(239,68,68,.12);
  color: #fca5a5;
  padding: 8px;
  border-radius: 8px;
  font: 400 12px/16px 'Roboto', sans-serif;
}

.pb-event-row {
  width: 100%;
  border: 1px solid var(--olv);
  border-radius: 8px;
  background: transparent;
  color: inherit;
  display: grid;
  grid-template-columns: auto auto 1fr auto;
  gap: 8px;
  align-items: center;
  padding: 8px;
  margin-bottom: 6px;
  cursor: pointer;
  text-align: left;
}

.pb-event-row:hover {
  background: rgba(200,191,255,.08);
}

.pe-dot {
  width: 8px;
  height: 8px;
  border-radius: 50%;
}

.pe-dot.person { background: #f97316; }
.pe-dot.motion { background: #38bdf8; }
.pe-dot.alert { background: #ef4444; }

.pe-time, .pe-dur {
  color: var(--on-sfv);
  font: 500 12px/16px 'Roboto', sans-serif;
}

.pe-text {
  font: 400 12px/16px 'Roboto', sans-serif;
}

.pb-events-empty {
  color: var(--on-sfv);
  font: 400 12px/16px 'Roboto', sans-serif;
  padding: 8px 0;
}

@media (max-width: 1180px) {
  .pb-layout {
    grid-template-columns: 1fr;
    height: auto;
  }

  .pb-sidebar {
    max-height: 500px;
  }

  .pb-controls-top {
    grid-template-columns: 1fr;
  }

  .pbc-center {
    justify-content: flex-start;
  }
}
</style>
