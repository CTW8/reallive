<script setup>
import { computed, nextTick, onBeforeUnmount, onMounted, ref, watch } from 'vue'
import mpegts from 'mpegts.js'
import { alertApi, cameraApi } from '../../api/index.js'

const loading = ref(false)
const error = ref('')
const alerts = ref([])

const activeType = ref('all')
const activeStatus = ref('all')
const timeRange = ref('today')
const keyword = ref('')
const currentPage = ref(1)
const pageSize = 8
const autoRefresh = ref(true)
const totalItems = ref(0)

const selectedIds = ref([])
const drawerAlert = ref(null)
const drawerLoading = ref(false)
const lastUpdatedAt = ref(null)
const drawerVideoRef = ref(null)
const previewLoading = ref(false)
const previewError = ref('')
const previewMode = ref('none')
const previewLabel = ref('')

let previewPlayer = null
let previewReplaySessionId = null
let previewReplayCameraId = null

let refreshTimer = null

const typeFilters = [
  { key: 'all', label: 'All', icon: null },
  { key: 'motion', label: 'Motion', icon: 'directions_run' },
  { key: 'alarm', label: 'Alarm', icon: 'crisis_alert' },
  { key: 'offline', label: 'Offline', icon: 'videocam_off' },
  { key: 'system', label: 'System', icon: 'info' },
]

const summary = computed(() => {
  const rows = alerts.value
  const total = rows.length
  let newCount = 0
  let readCount = 0
  let resolvedCount = 0
  let motionCount = 0
  let unresolved = 0

  for (const row of rows) {
    if (row.status === 'new') newCount += 1
    if (row.status === 'read') readCount += 1
    if (row.status === 'resolved') resolvedCount += 1
    if (row.status !== 'resolved') unresolved += 1
    if (getTypeGroup(row) === 'motion') motionCount += 1
  }

  return {
    total,
    newCount,
    readCount,
    resolvedCount,
    unresolved,
    motionCount,
  }
})

const typeCounts = computed(() => {
  const counts = { all: alerts.value.length, motion: 0, alarm: 0, offline: 0, system: 0 }
  for (const row of alerts.value) {
    const key = getTypeGroup(row)
    if (counts[key] == null) counts[key] = 0
    counts[key] += 1
  }
  return counts
})

const filteredAlerts = computed(() => alerts.value)
const totalPages = computed(() => Math.max(1, Math.ceil(totalItems.value / pageSize)))
const pagedAlerts = computed(() => alerts.value)

const pageInfo = computed(() => {
  const total = totalItems.value
  if (!total) return 'Showing 0-0 of 0 alerts'
  const start = (currentPage.value - 1) * pageSize + 1
  const end = Math.min(total, start + pagedAlerts.value.length - 1)
  return `Showing ${start}-${end} of ${total} alerts`
})

const allPageSelected = computed(() => {
  if (!pagedAlerts.value.length) return false
  const set = new Set(selectedIds.value)
  return pagedAlerts.value.every((row) => set.has(row.id))
})

const hasSelection = computed(() => selectedIds.value.length > 0)

const statCards = computed(() => [
  { label: 'Unresolved', value: summary.value.unresolved, icon: 'crisis_alert', iconClass: 'red' },
  { label: 'Motion Today', value: summary.value.motionCount, icon: 'directions_run', iconClass: 'orange' },
  { label: 'Read', value: summary.value.readCount, icon: 'mark_email_read', iconClass: 'purple' },
  { label: 'Resolved', value: summary.value.resolvedCount, icon: 'check_circle', iconClass: 'green' },
])

watch([activeType, activeStatus, timeRange, keyword], () => {
  currentPage.value = 1
  selectedIds.value = []
  void refreshAlerts({ silent: true })
})

watch(currentPage, (page) => {
  if (page > totalPages.value) currentPage.value = totalPages.value
  if (page < 1) currentPage.value = 1
  void refreshAlerts({ silent: true })
})

onMounted(() => {
  void refreshAlerts()
  refreshTimer = setInterval(() => {
    if (autoRefresh.value && !drawerAlert.value) {
      void refreshAlerts({ silent: true })
    }
  }, 10000)
})

onBeforeUnmount(() => {
  if (refreshTimer) {
    clearInterval(refreshTimer)
    refreshTimer = null
  }
  stopPreview()
})

async function refreshAlerts(options = {}) {
  const silent = Boolean(options.silent)
  if (!silent) {
    loading.value = true
    error.value = ''
  }
  try {
    const [since, until] = toRangeBounds(timeRange.value)
    const data = await alertApi.listPaged({
      page: currentPage.value,
      limit: pageSize,
      type_group: activeType.value !== 'all' ? activeType.value : undefined,
      status: activeStatus.value !== 'all' ? activeStatus.value : undefined,
      q: keyword.value.trim() || undefined,
      since,
      until,
    })
    alerts.value = Array.isArray(data?.items) ? data.items : []
    totalItems.value = Number(data?.total || 0)
    const idSet = new Set(alerts.value.map((row) => row.id))
    selectedIds.value = selectedIds.value.filter((id) => idSet.has(id))
    lastUpdatedAt.value = Date.now()
  } catch (err) {
    error.value = err?.message || 'Failed to load alerts'
  } finally {
    if (!silent) loading.value = false
  }
}

function getTypeGroup(row) {
  const raw = String(row?.type || '').toLowerCase()
  if (raw.includes('motion') || raw.includes('person')) return 'motion'
  if (raw.includes('offline') || raw.includes('disconnect')) return 'offline'
  if (raw.includes('alarm') || raw.includes('intrusion') || raw.includes('warning')) return 'alarm'
  return 'system'
}

function getIconByType(row) {
  const type = getTypeGroup(row)
  if (type === 'motion') return 'directions_run'
  if (type === 'offline') return 'videocam_off'
  if (type === 'alarm') return 'crisis_alert'
  return 'info'
}

function getIconClass(row) {
  const type = getTypeGroup(row)
  if (type === 'motion') return 'motion'
  if (type === 'offline') return 'offline'
  if (type === 'alarm') return 'alarm'
  return 'info'
}

function statusTagClass(status) {
  if (status === 'new') return 'new'
  if (status === 'resolved') return 'resolved'
  return 'read'
}

function statusLabel(status) {
  if (status === 'new') return 'New'
  if (status === 'resolved') return 'Resolved'
  return 'Read'
}

function formatClock(value) {
  if (!value) return '--'
  const date = new Date(value)
  if (Number.isNaN(date.getTime())) return '--'
  return date.toLocaleTimeString([], { hour: '2-digit', minute: '2-digit', second: '2-digit', hour12: false })
}

function formatDateTime(value) {
  if (!value) return '--'
  const date = new Date(value)
  if (Number.isNaN(date.getTime())) return '--'
  return date.toLocaleString([], { year: 'numeric', month: '2-digit', day: '2-digit', hour: '2-digit', minute: '2-digit', second: '2-digit' })
}

function extractAlertEventTsMs(alert) {
  const direct = Number(alert?.event_ts_ms || 0)
  if (Number.isFinite(direct) && direct > 0) return Math.floor(direct)
  const desc = String(alert?.description || '')
  const marker = desc.match(/\[evt:(\d{10,16}):/)
  if (marker && marker[1]) {
    const raw = Number(marker[1])
    if (Number.isFinite(raw) && raw > 0) {
      if (raw > 1e15) return Math.floor(raw / 1000)
      if (raw < 1e12) return Math.floor(raw * 1000)
      return Math.floor(raw)
    }
  }
  const created = new Date(alert?.created_at || 0).getTime()
  if (Number.isFinite(created) && created > 0) return created
  return Date.now()
}

function formatUpdatedAt(value) {
  if (!value) return '--'
  return new Date(value).toLocaleTimeString([], { hour: '2-digit', minute: '2-digit', second: '2-digit', hour12: false })
}

function formatRelative(value) {
  if (!value) return '--'
  const ts = new Date(value).getTime()
  if (!Number.isFinite(ts)) return '--'
  const deltaSec = Math.max(0, Math.floor((Date.now() - ts) / 1000))
  if (deltaSec < 60) return `${deltaSec}s ago`
  if (deltaSec < 3600) return `${Math.floor(deltaSec / 60)}m ago`
  if (deltaSec < 86400) return `${Math.floor(deltaSec / 3600)}h ago`
  return `${Math.floor(deltaSec / 86400)}d ago`
}

function formatRelativeAlert(row) {
  return formatRelative(extractAlertEventTsMs(row))
}

function toRangeBounds(range) {
  const now = new Date()
  if (range === 'all') return [undefined, undefined]
  if (range === 'today') {
    const start = new Date(now)
    start.setHours(0, 0, 0, 0)
    return [start.toISOString(), now.toISOString()]
  }
  if (range === '7d') {
    const start = new Date(now.getTime() - 7 * 24 * 3600 * 1000)
    return [start.toISOString(), now.toISOString()]
  }
  if (range === '30d') {
    const start = new Date(now.getTime() - 30 * 24 * 3600 * 1000)
    return [start.toISOString(), now.toISOString()]
  }
  return [undefined, undefined]
}

function toggleSelectAllPage(event) {
  const checked = Boolean(event?.target?.checked)
  const ids = pagedAlerts.value.map((row) => row.id)
  const set = new Set(selectedIds.value)
  if (checked) {
    for (const id of ids) set.add(id)
  } else {
    for (const id of ids) set.delete(id)
  }
  selectedIds.value = [...set]
}

function toggleRowSelection(id, checked) {
  const set = new Set(selectedIds.value)
  if (checked) set.add(id)
  else set.delete(id)
  selectedIds.value = [...set]
}

async function markOneRead(id) {
  try {
    await alertApi.markRead(id)
    await refreshAlerts({ silent: true })
  } catch (err) {
    error.value = err?.message || 'Failed to mark alert as read'
  }
}

async function resolveOne(id) {
  try {
    await alertApi.resolve(id)
    await refreshAlerts({ silent: true })
  } catch (err) {
    error.value = err?.message || 'Failed to resolve alert'
  }
}

async function deleteOne(id) {
  try {
    await alertApi.delete(id)
    selectedIds.value = selectedIds.value.filter((x) => x !== id)
    if (drawerAlert.value?.id === id) {
      await stopPreview()
      drawerAlert.value = null
    }
    await refreshAlerts({ silent: true })
  } catch (err) {
    error.value = err?.message || 'Failed to delete alert'
  }
}

async function applyBatch(action) {
  if (!selectedIds.value.length) return
  try {
    await alertApi.batchAction(selectedIds.value, action)
    selectedIds.value = []
    await refreshAlerts({ silent: true })
  } catch (err) {
    error.value = err?.message || `Batch ${action} failed`
  }
}

async function openDrawer(row) {
  if (!row?.id) return
  drawerLoading.value = true
  previewError.value = ''
  drawerAlert.value = row
  try {
    const detail = await alertApi.get(row.id)
    drawerAlert.value = detail || row
    if ((detail?.status || row.status) === 'new') {
      await markOneRead(row.id)
    }
  } catch {
    drawerAlert.value = row
  } finally {
    drawerLoading.value = false
  }
  await startPreviewForAlert(drawerAlert.value || row)
}

function closeDrawer() {
  stopPreview()
  drawerAlert.value = null
}

async function exportCurrent() {
  const rows = filteredAlerts.value
  const payload = rows.map((row) => ({
    id: row.id,
    type: row.type,
    title: row.title,
    description: row.description,
    camera: row.camera_name,
    status: row.status,
    created_at: row.created_at,
  }))
  const blob = new Blob([JSON.stringify(payload, null, 2)], { type: 'application/json;charset=utf-8' })
  const url = URL.createObjectURL(blob)
  const a = document.createElement('a')
  a.href = url
  a.download = `alerts-${new Date().toISOString().slice(0, 10)}.json`
  a.click()
  URL.revokeObjectURL(url)
}

function goPrevPage() {
  currentPage.value = Math.max(1, currentPage.value - 1)
}

function goNextPage() {
  currentPage.value = Math.min(totalPages.value, currentPage.value + 1)
}

function clearFilters() {
  activeType.value = 'all'
  activeStatus.value = 'all'
  timeRange.value = 'today'
  keyword.value = ''
}

async function startPreviewForAlert(alert) {
  await stopPreview()
  await nextTick()
  previewLoading.value = true
  previewError.value = ''
  previewMode.value = 'none'
  previewLabel.value = ''

  try {
    const cameraId = Number(alert?.camera_id || 0)
    if (!cameraId) {
      previewError.value = 'No camera bound to this alert'
      return
    }
    const tsMs = extractAlertEventTsMs(alert)
    if (!Number.isFinite(tsMs)) {
      previewError.value = 'Invalid alert timestamp'
      return
    }

    try {
      const playback = await cameraApi.getHistoryPlayback(cameraId, tsMs)
      if (playback?.mode === 'history' && playback?.playbackUrl) {
        previewReplaySessionId = playback.sessionId || null
        previewReplayCameraId = cameraId
        previewMode.value = 'history'
        previewLabel.value = 'History snapshot'
        if ((playback.transport || '').startsWith('flv')) {
          const url = playback.playbackUrl.startsWith('http')
            ? playback.playbackUrl
            : `${window.location.origin}${playback.playbackUrl}`
          startPreviewFlv(url)
        } else {
          await startPreviewFile(playback.playbackUrl, playback.offsetSec)
        }
        return
      }
    } catch {
    }

    const info = await cameraApi.getStreamInfo(cameraId)
    const streamKey = String(info?.stream_key || '')
    if (!streamKey) {
      previewError.value = 'Live stream unavailable'
      return
    }
    const liveUrl = `${window.location.origin}/live/${encodeURIComponent(streamKey)}.flv`
    previewMode.value = 'live'
    previewLabel.value = 'Live fallback'
    startPreviewFlv(liveUrl)
  } finally {
    previewLoading.value = false
  }
}

function startPreviewFlv(url) {
  const video = drawerVideoRef.value
  if (!video) {
    previewError.value = 'Preview player unavailable'
    return
  }
  if (!mpegts.isSupported()) {
    previewError.value = 'Browser does not support HTTP-FLV'
    return
  }
  stopPreviewPlayerOnly()
  clearPreviewVideoSrc()

  video.controls = true
  video.muted = true
  previewPlayer = mpegts.createPlayer({
    type: 'flv',
    isLive: true,
    url,
  }, {
    enableWorker: true,
    enableStashBuffer: false,
    lazyLoad: false,
    autoCleanupSourceBuffer: true,
  })
  previewPlayer.attachMediaElement(video)
  previewPlayer.on(mpegts.Events.ERROR, (_, detail) => {
    previewError.value = detail ? `Playback error: ${detail}` : 'Playback error'
  })
  previewPlayer.load()
  previewPlayer.play().catch(() => {})
}

async function startPreviewFile(playbackUrl, offsetSec) {
  const video = drawerVideoRef.value
  if (!video) {
    previewError.value = 'Preview player unavailable'
    return
  }
  stopPreviewPlayerOnly()
  clearPreviewVideoSrc()

  const url = playbackUrl.startsWith('http')
    ? playbackUrl
    : `${window.location.origin}${playbackUrl}`
  const seekSec = Math.max(0, Number(offsetSec) || 0)
  video.controls = true
  video.muted = true

  await new Promise((resolve) => {
    const onLoaded = () => {
      const duration = Number(video.duration)
      if (Number.isFinite(duration) && duration > 0) {
        video.currentTime = Math.min(seekSec, Math.max(0, duration - 0.2))
      } else {
        video.currentTime = seekSec
      }
      video.play().catch(() => {})
      video.removeEventListener('loadedmetadata', onLoaded)
      resolve()
    }
    video.addEventListener('loadedmetadata', onLoaded)
    video.src = url
    video.load()
  })
}

async function stopPreview() {
  stopPreviewPlayerOnly()
  clearPreviewVideoSrc()
  if (previewReplaySessionId && previewReplayCameraId) {
    try {
      await cameraApi.stopHistoryReplay(previewReplayCameraId, previewReplaySessionId)
    } catch {
    }
  }
  previewReplaySessionId = null
  previewReplayCameraId = null
  previewMode.value = 'none'
  previewLabel.value = ''
}

function stopPreviewPlayerOnly() {
  if (!previewPlayer) return
  try {
    previewPlayer.pause()
    previewPlayer.unload()
    previewPlayer.detachMediaElement()
    previewPlayer.destroy()
  } catch {
  }
  previewPlayer = null
}

function clearPreviewVideoSrc() {
  const video = drawerVideoRef.value
  if (!video) return
  try {
    video.pause()
    video.removeAttribute('src')
    video.load()
  } catch {
  }
}
</script>

<template>
  <div class="alerts-page">
    <div class="alert-stats-row">
      <div v-for="card in statCards" :key="card.label" class="alert-stat-mini">
        <div class="asm-icon stat-icon" :class="card.iconClass"><span class="mi">{{ card.icon }}</span></div>
        <div class="asm-info">
          <h4>{{ card.value }}</h4>
          <p>{{ card.label }}</p>
        </div>
      </div>
    </div>

    <div class="alert-toolbar">
      <div class="at-left">
        <div class="filter-chips">
          <button
            v-for="item in typeFilters"
            :key="item.key"
            class="filter-chip"
            :class="{ active: activeType === item.key }"
            @click="activeType = item.key"
          >
            <span v-if="item.icon" class="mi">{{ item.icon }}</span>
            {{ item.label }}
            <span class="chip-count">{{ typeCounts[item.key] || 0 }}</span>
          </button>
        </div>
      </div>
      <div class="at-right">
        <span class="at-updated">Updated {{ formatUpdatedAt(lastUpdatedAt) }}</span>
        <input v-model.trim="keyword" class="keyword-input" placeholder="Search title, camera, description">
        <select v-model="activeStatus" class="time-select">
          <option value="all">All Status</option>
          <option value="new">New</option>
          <option value="read">Read</option>
          <option value="resolved">Resolved</option>
        </select>
        <select v-model="timeRange" class="time-select">
          <option value="today">Today</option>
          <option value="7d">Last 7 Days</option>
          <option value="30d">Last 30 Days</option>
          <option value="all">All</option>
        </select>
        <button class="mt-btn" @click="clearFilters"><span class="mi">filter_alt_off</span> Clear</button>
        <button class="mt-btn" @click="refreshAlerts()"><span class="mi">refresh</span> Refresh</button>
        <button class="mt-btn" :class="{ active: autoRefresh }" @click="autoRefresh = !autoRefresh">
          <span class="mi">{{ autoRefresh ? 'pause_circle' : 'play_circle' }}</span>
          {{ autoRefresh ? 'Auto On' : 'Auto Off' }}
        </button>
        <button class="mt-btn" @click="exportCurrent"><span class="mi">download</span> Export</button>
      </div>
    </div>

    <div class="alert-table-wrap">
      <div class="alert-table-header">
        <div class="ath-left">
          <label class="select-all">
            <input type="checkbox" :checked="allPageSelected" @change="toggleSelectAllPage">
            <span>Select All</span>
          </label>
          <div class="batch-actions">
            <button class="batch-btn" :disabled="!hasSelection" @click="applyBatch('read')"><span class="mi">mark_email_read</span> Mark Read</button>
            <button class="batch-btn" :disabled="!hasSelection" @click="applyBatch('resolve')"><span class="mi">archive</span> Resolve</button>
            <button class="batch-btn danger" :disabled="!hasSelection" @click="applyBatch('delete')"><span class="mi">delete</span> Delete</button>
          </div>
        </div>
      </div>

      <div class="alert-table">
        <div class="at-row header">
          <div class="at-cb"></div>
          <div>Alert</div>
          <div>Camera</div>
          <div>Time</div>
          <div>Status</div>
          <div></div>
        </div>

        <div v-if="loading" class="table-state">Loading alerts…</div>
        <div v-else-if="error" class="table-state error">{{ error }}</div>
        <div v-else-if="!pagedAlerts.length" class="table-state">No alert found for current filters.</div>

        <div
          v-for="row in pagedAlerts"
          v-else
          :key="row.id"
          class="at-row"
          :class="{ unread: row.status === 'new' }"
          @click="openDrawer(row)"
        >
          <div class="at-cb" @click.stop>
            <input
              type="checkbox"
              :checked="selectedIds.includes(row.id)"
              @change="toggleRowSelection(row.id, $event.target.checked)"
            >
          </div>
          <div class="at-main">
            <div class="at-icon" :class="getIconClass(row)"><span class="mi">{{ getIconByType(row) }}</span></div>
            <div class="at-text">
              <h4>{{ row.title || 'Alert' }}</h4>
              <p>{{ row.description || '-' }}</p>
            </div>
          </div>
          <div class="at-camera">{{ row.camera_name || 'System' }}</div>
          <div class="at-time">{{ formatRelativeAlert(row) }}</div>
          <div class="at-status"><span class="status-tag" :class="statusTagClass(row.status)">{{ statusLabel(row.status) }}</span></div>
          <div class="at-actions" @click.stop>
            <button class="act-btn" title="View" @click="openDrawer(row)"><span class="mi">visibility</span></button>
            <button v-if="row.status === 'new'" class="act-btn" title="Mark Read" @click="markOneRead(row.id)"><span class="mi">check</span></button>
            <button v-if="row.status !== 'resolved'" class="act-btn" title="Resolve" @click="resolveOne(row.id)"><span class="mi">task_alt</span></button>
            <button class="act-btn danger" title="Delete" @click="deleteOne(row.id)"><span class="mi">delete</span></button>
          </div>
        </div>
      </div>

      <div class="table-pagination">
        <span class="tp-info">{{ pageInfo }}</span>
        <div class="tp-controls">
          <button class="page-btn" :class="{ disabled: currentPage <= 1 }" @click="goPrevPage"><span class="mi">chevron_left</span></button>
          <button class="page-btn active">{{ currentPage }}</button>
          <button class="page-btn" :class="{ disabled: currentPage >= totalPages }" @click="goNextPage"><span class="mi">chevron_right</span></button>
        </div>
      </div>
    </div>

    <div v-if="drawerAlert" class="alert-drawer-mask" @click.self="closeDrawer">
      <aside class="alert-drawer">
        <div class="drawer-head">
          <h3>Alert Detail</h3>
          <button class="drawer-close" @click="closeDrawer"><span class="mi">close</span></button>
        </div>
        <div v-if="drawerLoading" class="drawer-loading">Loading…</div>
        <div v-else class="drawer-body">
          <div class="drawer-preview">
            <div class="dp-head">
              <span>Event Preview</span>
              <span class="dp-mode">{{ previewLabel || 'No stream' }}</span>
            </div>
            <video ref="drawerVideoRef" class="dp-video" playsinline controls muted></video>
            <div v-if="previewLoading" class="dp-overlay">Loading preview...</div>
            <div v-else-if="previewError" class="dp-overlay error">{{ previewError }}</div>
          </div>
          <div class="drawer-row"><span class="label">Title</span><span class="value">{{ drawerAlert.title || '-' }}</span></div>
          <div class="drawer-row"><span class="label">Type</span><span class="value">{{ drawerAlert.type || '-' }}</span></div>
          <div class="drawer-row"><span class="label">Camera</span><span class="value">{{ drawerAlert.camera_name || 'System' }}</span></div>
          <div class="drawer-row"><span class="label">Status</span><span class="value">{{ statusLabel(drawerAlert.status) }}</span></div>
          <div class="drawer-row"><span class="label">Time</span><span class="value">{{ formatDateTime(extractAlertEventTsMs(drawerAlert)) }}</span></div>
          <div class="drawer-row multi"><span class="label">Description</span><p class="desc">{{ drawerAlert.description || '-' }}</p></div>
          <div class="drawer-actions">
            <button v-if="drawerAlert.status === 'new'" class="batch-btn" @click="markOneRead(drawerAlert.id)">Mark Read</button>
            <button v-if="drawerAlert.status !== 'resolved'" class="batch-btn" @click="resolveOne(drawerAlert.id)">Resolve</button>
            <button class="batch-btn danger" @click="deleteOne(drawerAlert.id)">Delete</button>
          </div>
        </div>
      </aside>
    </div>
  </div>
</template>

<style scoped>
.alerts-page {
  display: flex;
  flex-direction: column;
  gap: 16px;
}

.alert-stats-row {
  display: grid;
  grid-template-columns: repeat(4, 1fr);
  gap: 12px;
}

.alert-stat-mini {
  background: var(--sc);
  border-radius: var(--r3);
  padding: 16px;
  display: flex;
  align-items: center;
  gap: 14px;
}

.asm-icon {
  width: 40px;
  height: 40px;
  border-radius: var(--r2);
  display: flex;
  align-items: center;
  justify-content: center;
  flex-shrink: 0;
}

.stat-icon.red { background: rgba(255,107,107,.12); color: var(--red); }
.stat-icon.orange { background: rgba(255,158,67,.12); color: var(--orange); }
.stat-icon.purple { background: rgba(200,191,255,.12); color: var(--pri); }
.stat-icon.green { background: rgba(125,216,129,.12); color: var(--green); }

.asm-icon .mi { font-size: 20px; }
.asm-info h4 { font: 500 20px/26px 'Roboto', sans-serif; }
.asm-info p { font: 400 12px/16px 'Roboto', sans-serif; color: var(--on-sfv); }

.alert-toolbar {
  display: flex;
  align-items: center;
  justify-content: space-between;
  flex-wrap: wrap;
  gap: 12px;
}

.at-left,
.at-right {
  display: flex;
  align-items: center;
  gap: 8px;
  flex-wrap: wrap;
}

.at-updated {
  color: var(--on-sfv);
  font: 400 12px/16px 'Roboto', sans-serif;
}

.filter-chips {
  display: flex;
  gap: 6px;
  flex-wrap: wrap;
}

.filter-chip {
  display: inline-flex;
  align-items: center;
  gap: 5px;
  height: 32px;
  padding: 0 14px;
  border-radius: var(--r6);
  border: 1px solid var(--olv);
  font: 500 13px/18px 'Roboto', sans-serif;
  color: var(--on-sfv);
  background: transparent;
  cursor: pointer;
}

.filter-chip.active {
  background: var(--pri-c);
  color: var(--on-pri-c);
  border-color: var(--pri-c);
}

.filter-chip .mi {
  font-size: 15px;
}

.filter-chip .chip-count {
  background: rgba(255,255,255,.12);
  padding: 1px 6px;
  border-radius: 10px;
  font-size: 11px;
}

.time-select,
.mt-btn {
  height: 34px;
  padding: 0 12px;
  border-radius: var(--r6);
  border: 1px solid var(--olv);
  background: transparent;
  color: var(--on-sfv);
  font: 500 13px/18px 'Roboto', sans-serif;
}

.keyword-input {
  height: 34px;
  min-width: 240px;
  padding: 0 12px;
  border-radius: var(--r6);
  border: 1px solid var(--olv);
  background: var(--sc2);
  color: var(--on-sf);
  font: 400 13px/18px 'Roboto', sans-serif;
  outline: none;
}

.time-select {
  background: var(--sc2);
  outline: none;
}

.mt-btn {
  cursor: pointer;
  display: inline-flex;
  align-items: center;
  gap: 6px;
}

.mt-btn .mi {
  font-size: 18px;
}

.mt-btn.active {
  border-color: var(--pri);
  color: var(--pri);
}

.alert-table-wrap {
  background: var(--sc);
  border-radius: var(--r3);
  overflow: hidden;
}

.alert-table-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 14px 18px;
  border-bottom: 1px solid var(--olv);
}

.ath-left {
  display: flex;
  align-items: center;
  gap: 12px;
  flex-wrap: wrap;
}

.select-all {
  display: flex;
  align-items: center;
  gap: 8px;
  font: 400 13px/18px 'Roboto', sans-serif;
  color: var(--on-sfv);
}

.select-all input,
.at-cb input {
  width: 16px;
  height: 16px;
  accent-color: var(--pri);
}

.batch-actions {
  display: flex;
  gap: 6px;
  flex-wrap: wrap;
}

.batch-btn {
  height: 30px;
  padding: 0 12px;
  border-radius: var(--r6);
  border: 1px solid var(--olv);
  background: transparent;
  color: var(--on-sfv);
  font: 500 12px/16px 'Roboto', sans-serif;
  display: inline-flex;
  align-items: center;
  gap: 4px;
  cursor: pointer;
}

.batch-btn:disabled {
  opacity: .45;
  pointer-events: none;
}

.batch-btn.danger:hover {
  background: rgba(255,107,107,.12);
  color: var(--red);
  border-color: var(--red);
}

.batch-btn .mi {
  font-size: 15px;
}

.alert-table .at-row {
  display: grid;
  grid-template-columns: 40px 1fr 150px 130px 120px 84px;
  align-items: center;
  padding: 12px 18px;
  border-bottom: 1px solid var(--olv);
  cursor: pointer;
}

.alert-table .at-row.unread {
  background: rgba(200,191,255,.03);
}

.alert-table .at-row.header {
  font: 500 12px/16px 'Roboto', sans-serif;
  color: var(--on-sfv);
  text-transform: uppercase;
  letter-spacing: .5px;
  cursor: default;
}

.at-main {
  display: flex;
  align-items: center;
  gap: 12px;
  min-width: 0;
}

.at-icon {
  width: 34px;
  height: 34px;
  border-radius: 50%;
  display: flex;
  align-items: center;
  justify-content: center;
  flex-shrink: 0;
}

.at-icon .mi {
  font-size: 18px;
}

.at-icon.motion { background: rgba(255,158,67,.12); color: var(--orange); }
.at-icon.offline,
.at-icon.alarm { background: rgba(255,107,107,.12); color: var(--red); }
.at-icon.info { background: rgba(200,191,255,.12); color: var(--pri); }

.at-text {
  min-width: 0;
}

.at-text h4 {
  font: 500 14px/20px 'Roboto', sans-serif;
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}

.at-text p {
  font: 400 12px/16px 'Roboto', sans-serif;
  color: var(--on-sfv);
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}

.at-camera,
.at-time {
  font: 400 13px/18px 'Roboto', sans-serif;
  color: var(--on-sfv);
}

.status-tag {
  display: inline-flex;
  align-items: center;
  gap: 4px;
  padding: 3px 10px;
  border-radius: var(--r6);
  font: 500 11px/16px 'Roboto', sans-serif;
}

.status-tag.new { background: rgba(255,107,107,.12); color: var(--red); }
.status-tag.read { background: rgba(147,143,153,.15); color: var(--ol); }
.status-tag.resolved { background: rgba(125,216,129,.12); color: var(--green); }

.at-actions {
  display: flex;
  justify-content: flex-end;
  gap: 4px;
}

.act-btn {
  width: 28px;
  height: 28px;
  border-radius: 50%;
  border: none;
  background: transparent;
  display: flex;
  align-items: center;
  justify-content: center;
  cursor: pointer;
}

.act-btn .mi {
  font-size: 18px;
  color: var(--on-sfv);
}

.act-btn.danger:hover .mi {
  color: var(--red);
}

.table-state {
  padding: 24px;
  color: var(--on-sfv);
  font: 400 13px/18px 'Roboto', sans-serif;
}

.table-state.error {
  color: #fca5a5;
}

.table-pagination {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 14px 18px;
}

.tp-info {
  font: 400 13px/18px 'Roboto', sans-serif;
  color: var(--on-sfv);
}

.tp-controls {
  display: flex;
  align-items: center;
  gap: 4px;
}

.page-btn {
  width: 32px;
  height: 32px;
  border-radius: var(--r2);
  border: 1px solid var(--olv);
  background: transparent;
  color: var(--on-sfv);
  display: flex;
  align-items: center;
  justify-content: center;
  cursor: pointer;
}

.page-btn.active {
  background: var(--pri-c);
  color: var(--on-pri-c);
  border-color: var(--pri-c);
}

.page-btn.disabled {
  opacity: .35;
  pointer-events: none;
}

.alert-drawer-mask {
  position: fixed;
  inset: 0;
  background: rgba(2, 6, 23, .6);
  z-index: 50;
  display: flex;
  justify-content: flex-end;
}

.alert-drawer {
  width: min(420px, 92vw);
  height: 100%;
  background: var(--sc);
  border-left: 1px solid var(--olv);
  display: flex;
  flex-direction: column;
}

.drawer-head {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 14px 16px;
  border-bottom: 1px solid var(--olv);
}

.drawer-head h3 {
  font: 500 16px/22px 'Roboto', sans-serif;
  margin: 0;
}

.drawer-close {
  width: 30px;
  height: 30px;
  border-radius: 50%;
  border: 1px solid var(--olv);
  background: transparent;
  color: var(--on-sfv);
}

.drawer-loading {
  padding: 20px;
  color: var(--on-sfv);
}

.drawer-body {
  padding: 14px 16px;
  display: flex;
  flex-direction: column;
  gap: 12px;
}

.drawer-preview {
  position: relative;
  border: 1px solid var(--olv);
  border-radius: 10px;
  overflow: hidden;
  background: #000;
}

.dp-head {
  height: 30px;
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 0 10px;
  background: rgba(20, 20, 28, 0.85);
  border-bottom: 1px solid var(--olv);
  font: 500 12px/16px 'Roboto', sans-serif;
  color: var(--on-sfv);
}

.dp-mode {
  color: var(--pri);
}

.dp-video {
  width: 100%;
  height: 180px;
  object-fit: cover;
  background: #000;
  display: block;
}

.dp-overlay {
  position: absolute;
  inset: 30px 0 0 0;
  display: flex;
  align-items: center;
  justify-content: center;
  background: rgba(2, 6, 23, 0.45);
  color: var(--on-sfv);
  font: 500 13px/18px 'Roboto', sans-serif;
}

.dp-overlay.error {
  color: #fecaca;
}

.drawer-row {
  display: grid;
  grid-template-columns: 90px 1fr;
  gap: 8px;
}

.drawer-row .label {
  color: var(--on-sfv);
  font: 500 12px/16px 'Roboto', sans-serif;
}

.drawer-row .value {
  font: 400 13px/18px 'Roboto', sans-serif;
}

.drawer-row.multi {
  grid-template-columns: 1fr;
}

.desc {
  margin: 0;
  padding: 10px;
  border-radius: 8px;
  background: var(--sc2);
  border: 1px solid var(--olv);
  font: 400 13px/18px 'Roboto', sans-serif;
}

.drawer-actions {
  margin-top: 10px;
  display: flex;
  gap: 8px;
}

@media (max-width: 1439px) {
  .alert-stats-row {
    grid-template-columns: repeat(2, 1fr);
  }

  .alert-table .at-row {
    grid-template-columns: 40px 1fr 130px 110px 100px 84px;
  }
}

@media (max-width: 1023px) {
  .alert-stats-row {
    grid-template-columns: 1fr;
  }

  .keyword-input {
    min-width: 180px;
    flex: 1 1 180px;
  }

  .alert-table .at-row {
    grid-template-columns: 40px 1fr 100px 84px;
  }

  .at-camera,
  .at-status {
    display: none;
  }
}
</style>
