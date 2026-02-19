<script setup>
import { computed, onBeforeUnmount, onMounted, ref } from 'vue'
import { useRouter } from 'vue-router'
import { useAuthStore } from '../stores/auth.js'
import { useCameraStore } from '../stores/camera.js'
import { useDashboardStore } from '../stores/dashboard.js'
import { connectSignaling } from '../api/signaling.js'
import { cameraApi } from '../api/index.js'
import { adaptDashboardViewModel } from '../api/design-adapters.js'
import { heartbeatClassFromLabel, heartbeatLabelFromTs, heartbeatTsFromRuntime, formatUpdatedAgo } from '../utils/heartbeat.js'

const router = useRouter()
const auth = useAuthStore()
const cameraStore = useCameraStore()
const dashboardStore = useDashboardStore()

let socket = null
let refreshInterval = null
let cameraRefreshInterval = null
const failedThumbnailByCamera = ref({})

const camerasWithEventState = computed(() => {
  const states = dashboardStore.cameraEventStates || {}
  return cameraStore.cameras.map((camera) => ({
    ...camera,
    personEventState: states[camera.id] || { unread: 0, total: 0, lastTimestamp: null },
  }))
})

const dashboardVm = computed(() => adaptDashboardViewModel({
  stats: dashboardStore.stats,
  cameras: camerasWithEventState.value,
  activityEvents: dashboardStore.activityEvents,
}))

onMounted(async () => {
  await dashboardStore.fetchHealth()

  try {
    await cameraStore.fetchCameras()
  } catch (err) {
    if (err?.status === 401) {
      auth.logout()
      router.push('/login')
      return
    }
  }

  dashboardStore.fetchStats()
  dashboardStore.fetchRecentSessions()
  await hydrateCameraEventCounts()

  try {
    socket = connectSignaling(auth.token)
    dashboardStore.setSocketConnected(socket.connected)

    const joinDashboardRoom = () => {
      socket.emit('join-room', { room: `dashboard-${auth.user?.id}` })
    }

    socket.on('connect', () => {
      dashboardStore.setSocketConnected(true)
      joinDashboardRoom()
    })
    socket.on('disconnect', () => dashboardStore.setSocketConnected(false))
    if (socket.connected) joinDashboardRoom()

    socket.on('camera-status', ({ cameraId, status, thumbnailUrl, runtime }) => {
      const changed = cameraStore.updateCameraStatus(cameraId, status, {
        thumbnailUrl,
        device: runtime || null,
      })
      if (changed) {
        dashboardStore.fetchStats()
        dashboardStore.fetchRecentSessions()
      }
    })

    socket.on('activity-event', (event) => {
      dashboardStore.pushActivityEvent(event)
    })
  } catch {
  }

  refreshInterval = setInterval(() => {
    dashboardStore.fetchStats()
    dashboardStore.fetchHealth()
  }, 30000)

  cameraRefreshInterval = setInterval(() => {
    cameraStore.fetchCameras().catch(() => {})
  }, 10000)
})

onBeforeUnmount(() => {
  if (refreshInterval) clearInterval(refreshInterval)
  if (cameraRefreshInterval) clearInterval(cameraRefreshInterval)
  if (socket) {
    socket.off('camera-status')
    socket.off('activity-event')
    socket.off('connect')
    socket.off('disconnect')
  }
})

async function hydrateCameraEventCounts() {
  const cameras = cameraStore.cameras || []
  if (!cameras.length) return
  await Promise.all(cameras.map(async (camera) => {
    try {
      const overview = await cameraApi.getHistoryOverview(camera.id)
      if (!overview) return
      dashboardStore.setCameraEventTotal(camera.id, Number(overview.eventCount || 0), null)
    } catch {
    }
  }))
}

function handleWatch(camera) {
  dashboardStore.clearCameraUnread(camera.id)
  router.push(`/watch/${camera.id}`)
}

function navigateTo(page) {
  router.push(`/${page}`)
}

function resolveCameraLocation(camera) {
  const explicit = String(camera?.location || '').trim()
  if (explicit) return explicit
  const name = String(camera?.name || '').toLowerCase()
  if (name.includes('front') || name.includes('door')) return 'Main Entrance'
  if (name.includes('parking')) return 'Underground Level 2'
  if (name.includes('lobby')) return 'Building A, 1F'
  if (name.includes('warehouse')) return 'East Wing'
  if (name.includes('corridor')) return 'Building A, 3F'
  if (name.includes('server')) return 'Building B, B1'
  return 'Not configured'
}

function cameraStatusLabel(camera) {
  const status = String(camera?.status || 'offline').toLowerCase()
  if (status === 'offline') return 'Offline'
  if (status === 'recording' || status === 'streaming') return 'Streaming'
  return 'Online'
}

function cameraStatusDotClass(camera) {
  const status = String(camera?.status || 'offline').toLowerCase()
  if (status === 'offline') return 'off'
  if (status === 'recording' || status === 'streaming') return 'rec'
  return 'on'
}

function cameraStatusPillClass(camera) {
  const status = String(camera?.status || 'offline').toLowerCase()
  if (status === 'streaming' || status === 'recording') return 'pill-rec'
  if (status === 'online') return 'pill-on'
  return 'pill-off'
}

function cameraLastUpdateLabel(camera) {
  return formatUpdatedAgo(heartbeatTsFromRuntime(camera?.device || null))
}

function thumbnailVisible(camera) {
  if (!camera?.thumbnailUrl) return false
  return failedThumbnailByCamera.value[camera.id] !== camera.thumbnailUrl
}

function onThumbnailError(camera) {
  if (!camera?.id || !camera?.thumbnailUrl) return
  failedThumbnailByCamera.value[camera.id] = camera.thumbnailUrl
}

function onThumbnailLoad(camera) {
  if (!camera?.id) return
  if (failedThumbnailByCamera.value[camera.id]) {
    delete failedThumbnailByCamera.value[camera.id]
  }
}

function cameraHeartbeatLabel(camera) {
  return heartbeatLabelFromTs(heartbeatTsFromRuntime(camera?.device || null))
}

function cameraHeartbeatClass(camera) {
  return heartbeatClassFromLabel(cameraHeartbeatLabel(camera))
}
</script>

<template>
  <div class="dashboard">
    <div class="stats-row">
      <div v-for="card in dashboardVm.statsCards" :key="card.key" class="stat-card">
        <div class="stat-info">
          <h3>{{ card.value }}</h3>
          <p>{{ card.title }}</p>
          <div class="stat-trend" :class="card.trendClass"><span class="mi">trending_up</span> {{ card.trend }}</div>
        </div>
        <div class="stat-icon" :class="card.iconClass"><span class="mi">{{ card.icon }}</span></div>
      </div>
    </div>

    <div class="dash-grid">
      <div>
        <div class="section-header">
          <h3>All Cameras</h3>
          <a class="link" @click="navigateTo('monitor')">Manage <span class="mi">arrow_forward</span></a>
        </div>

        <div v-if="cameraStore.loading" class="loading-state">Loading cameras...</div>
        <div v-else-if="cameraStore.cameras.length === 0" class="empty-state">
          <p>No cameras configured yet.</p>
          <button class="btn-primary" style="max-width:220px" @click="navigateTo('devices')">+ Add Your First Camera</button>
        </div>

        <div v-else class="camera-grid">
          <div v-for="camera in camerasWithEventState.slice(0, 6)" :key="camera.id" class="cam-card" @click="handleWatch(camera)">
            <div class="cam-thumb" :style="{ opacity: camera.status === 'offline' ? 0.5 : 1 }">
              <img
                v-if="thumbnailVisible(camera)"
                class="cam-thumb-img"
                :src="camera.thumbnailUrl"
                :alt="camera.name"
                loading="lazy"
                @error="onThumbnailError(camera)"
                @load="onThumbnailLoad(camera)"
              />
              <div class="sv-lines"></div>
              <div class="sv-grid"></div>
              <div v-if="!thumbnailVisible(camera)" class="sv-icon"><span class="mi">{{ camera.status === 'offline' ? 'videocam_off' : 'videocam' }}</span></div>
              <div v-if="camera.status !== 'offline'" class="scan"></div>
              <div class="status-pill" :class="cameraStatusPillClass(camera)">
                <span class="dot"></span>{{ cameraStatusLabel(camera) }}
              </div>
              <div class="hb-pill" :class="cameraHeartbeatClass(camera)">{{ cameraHeartbeatLabel(camera) }}</div>
              <div v-if="camera.status !== 'offline'" class="ts">{{ new Date().toLocaleTimeString().slice(0, 8) }}</div>
            </div>
            <div class="cam-info">
              <h4>{{ camera.name }}</h4>
              <div class="cam-meta">
                <div class="cam-loc"><span class="mi">location_on</span><span>{{ resolveCameraLocation(camera) }}</span></div>
                <div class="cam-status"><span class="dot" :class="cameraStatusDotClass(camera)"></span>{{ cameraStatusLabel(camera) }}</div>
              </div>
              <div class="cam-updated">{{ cameraLastUpdateLabel(camera) }}</div>
            </div>
          </div>
        </div>
      </div>

      <div class="alert-panel">
        <div class="ap-header"><h3>Recent Alerts</h3><a class="link" @click="navigateTo('alerts')">View All</a></div>
        <div class="alert-list">
          <div v-for="alert in dashboardVm.recentAlerts" :key="alert.id" class="alert-item">
            <div class="ai-icon" :class="alert.type"><span class="mi">{{ alert.icon }}</span></div>
            <div class="ai-content">
              <h4>{{ alert.title }}</h4>
              <p>{{ alert.desc }}</p>
              <div class="ai-time">{{ alert.time }}</div>
            </div>
          </div>
        </div>
      </div>
    </div>
  </div>
</template>

<style scoped>
.dashboard { display: flex; flex-direction: column; gap: 18px; }
.stats-row { display: grid; grid-template-columns: repeat(4, 1fr); gap: 16px; margin-bottom: 4px; }
.stat-card { background: var(--sc); border-radius: var(--r3); padding: 20px; display: flex; align-items: flex-start; justify-content: space-between; transition: transform .15s, box-shadow .15s; }
.stat-card:hover { transform: translateY(-2px); box-shadow: var(--e2); }
.stat-card .stat-info h3 { font: 500 28px/36px 'Roboto', sans-serif; margin-bottom: 4px; }
.stat-card .stat-info p { font: 400 13px/18px 'Roboto', sans-serif; color: var(--on-sfv); }
.stat-trend { display: flex; align-items: center; gap: 3px; margin-top: 8px; font: 500 12px/16px 'Roboto', sans-serif; }
.stat-trend .mi { font-size: 16px; }
.stat-trend.up { color: var(--green); }
.stat-trend.down { color: var(--red); }
.stat-card .stat-icon { width: 44px; height: 44px; border-radius: var(--r3); display: flex; align-items: center; justify-content: center; }
.stat-card .stat-icon .mi { font-size: 24px; }
.stat-icon.green { background: rgba(125,216,129,.12); color: var(--green); }
.stat-icon.red { background: rgba(255,107,107,.12); color: var(--red); }
.stat-icon.orange { background: rgba(255,158,67,.12); color: var(--orange); }
.stat-icon.purple { background: rgba(200,191,255,.12); color: var(--pri); }

.section-header { display: flex; align-items: center; justify-content: space-between; margin-bottom: 16px; }
.section-header h3 { font: 500 16px/24px 'Roboto', sans-serif; }
.section-header .link { font: 500 13px/18px 'Roboto', sans-serif; color: var(--pri); text-decoration: none; cursor: pointer; display: flex; align-items: center; gap: 4px; }
.section-header .link .mi { font-size: 20px; }
.section-header .link:hover { text-decoration: underline; }

.dash-grid { display: grid; grid-template-columns: 1fr 360px; gap: 20px; margin-bottom: 24px; }
.camera-grid { display: grid; grid-template-columns: repeat(auto-fill, minmax(260px, 1fr)); gap: 14px; }
.cam-card { background: var(--sc); border-radius: var(--r3); overflow: hidden; cursor: pointer; transition: transform .15s, box-shadow .15s; }
.cam-card:hover { transform: translateY(-2px); box-shadow: var(--e2); }
.cam-thumb { width: 100%; aspect-ratio: 16/9; position: relative; overflow: hidden; background: linear-gradient(135deg, #0d0d15, #1c1728); }
.cam-thumb-img { position: absolute; inset: 0; width: 100%; height: 100%; object-fit: cover; }
.cam-thumb .sv-lines { position: absolute; inset: 0; background: repeating-linear-gradient(0deg, transparent, transparent 2px, rgba(255,255,255,.01) 2px, rgba(255,255,255,.01) 4px); }
.cam-thumb .sv-grid { position: absolute; inset: 0; background: linear-gradient(rgba(255,255,255,.02) 1px, transparent 1px), linear-gradient(90deg, rgba(255,255,255,.02) 1px, transparent 1px); background-size: 24px 24px; }
.cam-thumb .sv-icon { position: absolute; inset: 0; display: flex; align-items: center; justify-content: center; }
.cam-thumb .sv-icon .mi { font-size: 36px; color: rgba(255,255,255,.06); }
.cam-thumb .scan { position: absolute; top: 0; left: 0; right: 0; height: 2px; background: rgba(200,191,255,.08); animation: scan 3s linear infinite; }
.cam-thumb .status-pill { position: absolute; top: 8px; left: 8px; display: inline-flex; align-items: center; gap: 5px; padding: 3px 8px; border-radius: var(--r1); font: 500 10px/16px 'Roboto', sans-serif; color: #fff; backdrop-filter: blur(3px); }
.cam-thumb .status-pill .dot { width: 6px; height: 6px; border-radius: 50%; background: #fff; }
.cam-thumb .status-pill.pill-rec { background: rgba(212, 98, 72, .92); }
.cam-thumb .status-pill.pill-on { background: rgba(50, 142, 94, .92); }
.cam-thumb .status-pill.pill-off { background: rgba(90, 90, 90, .92); color: #ddd; }
.cam-thumb .hb-pill { position: absolute; top: 8px; right: 8px; padding: 2px 7px; border-radius: var(--r1); font: 600 10px/14px 'Roboto', sans-serif; letter-spacing: .3px; }
.cam-thumb .hb-pill.hb-good { background: rgba(50, 142, 94, .2); color: #98e2b0; border: 1px solid rgba(50, 142, 94, .4); }
.cam-thumb .hb-pill.hb-weak { background: rgba(255, 158, 67, .2); color: #ffc38e; border: 1px solid rgba(255, 158, 67, .4); }
.cam-thumb .hb-pill.hb-stale { background: rgba(255, 107, 107, .2); color: #ffb3b3; border: 1px solid rgba(255, 107, 107, .4); }
.cam-thumb .ts { position: absolute; bottom: 6px; right: 6px; padding: 2px 6px; border-radius: var(--r1); background: rgba(0,0,0,.6); font: 500 10px/16px 'Roboto', sans-serif; color: #fff; }
.cam-info { padding: 12px 14px; }
.cam-info h4 { font: 500 14px/20px 'Roboto', sans-serif; margin-bottom: 3px; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }
.cam-info .cam-meta { display: flex; align-items: center; justify-content: space-between; }
.cam-info .cam-loc { display: flex; align-items: center; gap: 4px; font: 400 12px/16px 'Roboto', sans-serif; color: var(--on-sfv); min-width: 0; }
.cam-info .cam-loc .mi { font-size: 14px; }
.cam-info .cam-loc span { white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }
.cam-info .cam-status { display: flex; align-items: center; gap: 5px; font: 500 11px/16px 'Roboto', sans-serif; text-transform: capitalize; }
.cam-info .cam-status .dot { width: 7px; height: 7px; border-radius: 50%; }
.cam-info .cam-status .dot.on { background: var(--green); }
.cam-info .cam-status .dot.off { background: var(--red); }
.cam-info .cam-status .dot.rec { background: var(--orange); }
.cam-info .cam-updated { margin-top: 4px; font: 400 11px/16px 'Roboto', sans-serif; color: var(--ol); }

.alert-panel { background: var(--sc); border-radius: var(--r3); display: flex; flex-direction: column; max-height: 600px; }
.alert-panel .ap-header { display: flex; align-items: center; justify-content: space-between; padding: 16px 18px 12px; border-bottom: 1px solid var(--olv); }
.alert-panel .ap-header h3 { font: 500 15px/22px 'Roboto', sans-serif; }
.alert-panel .ap-header .link { font: 500 12px/16px 'Roboto', sans-serif; color: var(--pri); cursor: pointer; }
.alert-list { flex: 1; overflow-y: auto; padding: 0 14px; scrollbar-width: none; }
.alert-list::-webkit-scrollbar { display: none; }
.alert-item { display: flex; gap: 12px; padding: 13px 0; border-bottom: 1px solid var(--olv); }
.alert-item:last-child { border-bottom: none; }
.alert-item .ai-icon { width: 36px; height: 36px; border-radius: 50%; display: flex; align-items: center; justify-content: center; flex-shrink: 0; }
.alert-item .ai-icon.motion { background: rgba(255,158,67,.12); color: var(--orange); }
.alert-item .ai-icon.alarm { background: rgba(255,107,107,.12); color: var(--red); }
.alert-item .ai-icon.info { background: rgba(200,191,255,.12); color: var(--pri); }
.alert-item .ai-icon .mi { font-size: 18px; }
.alert-item .ai-content { flex: 1; min-width: 0; }
.alert-item .ai-content h4 { font: 500 13px/18px 'Roboto', sans-serif; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }
.alert-item .ai-content p { font: 400 12px/16px 'Roboto', sans-serif; color: var(--on-sfv); margin-top: 2px; display: -webkit-box; -webkit-box-orient: vertical; -webkit-line-clamp: 2; overflow: hidden; }
.alert-item .ai-content .ai-time { font: 400 11px/16px 'Roboto', sans-serif; color: var(--ol); margin-top: 3px; }

.loading-state, .empty-state { text-align: center; padding: 60px 20px; color: var(--on-sfv); background: var(--sc); border: 1px solid var(--olv); border-radius: var(--r3); }
.empty-state p { margin-bottom: 16px; }

@keyframes scan {
  0% { transform: translateY(0); }
  100% { transform: translateY(100%); }
}

@keyframes blink {
  0%, 60%, 100% { opacity: 1; }
  30% { opacity: .3; }
}

.blink { animation: blink 1.3s infinite; }

@media (max-width: 1439px) {
  .stats-row { grid-template-columns: repeat(2, 1fr); }
  .dash-grid { grid-template-columns: 1fr 300px; }
}

@media (max-width: 1023px) {
  .dash-grid { grid-template-columns: 1fr; }
}

@media (max-width: 768px) {
  .stats-row, .camera-grid { grid-template-columns: 1fr; }
}
</style>
