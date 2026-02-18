<script setup>
import { ref, computed, onMounted, onBeforeUnmount } from 'vue'
import { useRouter } from 'vue-router'
import { useCameraStore } from '../stores/camera.js'
import { useDashboardStore } from '../stores/dashboard.js'
import { useAuthStore } from '../stores/auth.js'
import { connectSignaling } from '../api/signaling.js'
import { cameraApi } from '../api/index.js'

const router = useRouter()
const cameraStore = useCameraStore()
const dashboardStore = useDashboardStore()
const auth = useAuthStore()

let socket = null
let refreshInterval = null
let cameraRefreshInterval = null

const streamingCount = computed(() => dashboardStore.stats.cameras?.streaming || 0)

const camerasWithEventState = computed(() => {
  const states = dashboardStore.cameraEventStates || {}
  return cameraStore.cameras.map((camera) => ({
    ...camera,
    personEventState: states[camera.id] || { unread: 0, total: 0, lastTimestamp: null },
  }))
})

const recentAlerts = [
  { id: 1, type: 'motion', title: 'Motion Detected', desc: 'Front Door Camera - Unusual activity detected in restricted zone', time: '2 minutes ago' },
  { id: 2, type: 'alarm', title: 'Camera Offline', desc: 'Warehouse East - Connection lost, last seen 14:20:33', time: '5 minutes ago' },
  { id: 3, type: 'motion', title: 'Motion Detected', desc: 'Parking Lot B2 - Vehicle movement detected after hours', time: '12 minutes ago' },
  { id: 4, type: 'info', title: 'Storage Warning', desc: 'Cloud storage usage at 85% - consider upgrading plan', time: '28 minutes ago' },
  { id: 5, type: 'alarm', title: 'Intrusion Alert', desc: 'Corridor 3F North - Unauthorized access attempt detected', time: '45 minutes ago' },
]

onMounted(async () => {
  await dashboardStore.fetchHealth()

  try {
    await cameraStore.fetchCameras()
  } catch (err) {
    console.error('[Dashboard] Failed to fetch cameras:', err)
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
    if (socket.connected) {
      joinDashboardRoom()
    }

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
  } catch (err) {
    console.error('[Dashboard] Failed to connect signaling:', err)
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
  if (refreshInterval) {
    clearInterval(refreshInterval)
  }
  if (cameraRefreshInterval) {
    clearInterval(cameraRefreshInterval)
  }
  if (socket) {
    socket.off('camera-status')
    socket.off('activity-event')
    socket.off('connect')
    socket.off('disconnect')
  }
})

function handleWatch(camera) {
  dashboardStore.clearCameraUnread(camera.id)
  router.push(`/watch/${camera.id}`)
}

function navigateTo(page) {
  router.push(`/${page}`)
}

async function hydrateCameraEventCounts() {
  const cameras = cameraStore.cameras || []
  if (!cameras.length) return

  const tasks = cameras.map(async (camera) => {
    try {
      const overview = await cameraApi.getHistoryOverview(camera.id)
      if (!overview) return
      dashboardStore.setCameraEventTotal(
        camera.id,
        Number(overview.eventCount || 0),
        null
      )
    } catch {}
  })
  await Promise.all(tasks)
}
</script>

<template>
  <div class="dashboard">
    <div class="stats-row">
      <div class="stat-card">
        <div class="stat-info">
          <h3>{{ dashboardStore.stats.cameras?.online || 0 }}</h3>
          <p>Online Cameras</p>
        </div>
        <div class="stat-icon green"><span class="mi">videocam</span></div>
      </div>
      <div class="stat-card">
        <div class="stat-info">
          <h3>{{ dashboardStore.stats.cameras?.offline || 0 }}</h3>
          <p>Offline Cameras</p>
        </div>
        <div class="stat-icon red"><span class="mi">videocam_off</span></div>
      </div>
      <div class="stat-card">
        <div class="stat-info">
          <h3>5</h3>
          <p>Active Alerts</p>
        </div>
        <div class="stat-icon orange"><span class="mi">warning</span></div>
      </div>
      <div class="stat-card">
        <div class="stat-info">
          <h3>{{ streamingCount }}</h3>
          <p>Recording Now</p>
        </div>
        <div class="stat-icon purple"><span class="mi">fiber_manual_record</span></div>
      </div>
    </div>

    <div class="section-header"><h3>Quick Access</h3></div>
    <div class="quick-grid">
      <div class="quick-item" @click="navigateTo('monitor')">
        <div class="qi-top">
          <div class="qi-icon"><span class="mi">grid_view</span></div>
          <span class="qi-pill">Live</span>
        </div>
        <h4 class="qi-title">Multi View Wall</h4>
        <p class="qi-desc">Jump to Live Monitor and load default 2x2 watch layout.</p>
        <div class="qi-foot">
          <span class="qi-meta">{{ cameraStore.cameras.length }} cameras online</span>
          <button class="qi-btn" @click.stop="navigateTo('monitor')">Open</button>
        </div>
      </div>
      <div class="quick-item" @click="navigateTo('playback')">
        <div class="qi-top">
          <div class="qi-icon"><span class="mi">replay</span></div>
          <span class="qi-pill">Review</span>
        </div>
        <h4 class="qi-title">Incident Replay</h4>
        <p class="qi-desc">Open timeline playback with recent alert time range preselected.</p>
        <div class="qi-foot">
          <span class="qi-meta">Last alert: 2 min ago</span>
          <button class="qi-btn" @click.stop="navigateTo('playback')">Open</button>
        </div>
      </div>
      <div class="quick-item" @click="navigateTo('devices')">
        <div class="qi-top">
          <div class="qi-icon"><span class="mi">add_circle</span></div>
          <span class="qi-pill">Setup</span>
        </div>
        <h4 class="qi-title">Device Onboarding</h4>
        <p class="qi-desc">Go to Device Management and continue camera onboarding flow.</p>
        <div class="qi-foot">
          <span class="qi-meta">{{ dashboardStore.stats.cameras?.offline || 0 }} pending joins</span>
          <button class="qi-btn" @click.stop="navigateTo('devices')">Open</button>
        </div>
      </div>
      <div class="quick-item" @click="navigateTo('alerts')">
        <div class="qi-top">
          <div class="qi-icon"><span class="mi">download</span></div>
          <span class="qi-pill">Ops</span>
        </div>
        <h4 class="qi-title">Export Report</h4>
        <p class="qi-desc">Generate operation summary package (PDF + CSV + audit snapshot).</p>
        <div class="qi-foot">
          <span class="qi-meta">Last export: 09:30</span>
          <button class="qi-btn" @click.stop="navigateTo('alerts')">Run</button>
        </div>
      </div>
    </div>

    <div class="dash-grid">
      <div>
        <div class="section-header">
          <h3>Camera Preview</h3>
          <a class="link" @click="navigateTo('monitor')">View All <span class="mi">arrow_forward</span></a>
        </div>
        <div v-if="cameraStore.loading" class="loading-state">
          Loading cameras...
        </div>
        <div v-else-if="cameraStore.cameras.length === 0" class="empty-state">
          <p>No cameras configured yet.</p>
          <button class="btn-primary" style="max-width:200px" @click="navigateTo('devices')">
            + Add Your First Camera
          </button>
        </div>
        <div v-else class="camera-grid">
          <div
            v-for="camera in camerasWithEventState.slice(0, 6)"
            :key="camera.id"
            class="cam-card"
            @click="handleWatch(camera)"
          >
            <div class="cam-thumb" :style="{ opacity: camera.status === 'offline' ? 0.5 : 1 }">
              <div class="sv-lines"></div>
              <div class="sv-grid"></div>
              <div class="sv-icon"><span class="mi">{{ camera.status === 'offline' ? 'videocam_off' : 'videocam' }}</span></div>
              <div v-if="camera.status !== 'offline'" class="scan"></div>
              <div v-if="camera.status === 'streaming'" class="live-badge"><span class="dot blink"></span> REC</div>
              <div v-if="camera.status === 'offline'" class="offline-badge">OFFLINE</div>
              <div v-if="camera.status !== 'offline'" class="ts">{{ new Date().toLocaleTimeString().slice(0, 8) }}</div>
            </div>
            <div class="cam-info">
              <h4>{{ camera.name }}</h4>
              <div class="cam-meta">
                <div class="cam-loc"><span class="mi">location_on</span><span>{{ camera.resolution || 'Default' }}</span></div>
                <div class="cam-status">
                  <span class="dot" :class="{ on: camera.status === 'online', off: camera.status === 'offline', rec: camera.status === 'streaming' }"></span>
                  {{ camera.status === 'streaming' ? 'Recording' : camera.status }}
                </div>
              </div>
            </div>
          </div>
        </div>
      </div>

      <div class="alert-panel">
        <div class="ap-header">
          <h3>Recent Alerts</h3>
          <a class="link" @click="navigateTo('alerts')">View All</a>
        </div>
        <div class="alert-list">
          <div v-for="alert in recentAlerts" :key="alert.id" class="alert-item">
            <div class="ai-icon" :class="alert.type">
              <span class="mi">{{ alert.type === 'motion' ? 'directions_run' : alert.type === 'alarm' ? 'warning' : 'info' }}</span>
            </div>
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
.dashboard {
  display: flex;
  flex-direction: column;
  gap: 24px;
}

.stats-row {
  display: grid;
  grid-template-columns: repeat(4, 1fr);
  gap: 16px;
}

.stat-card {
  background: var(--sc);
  border-radius: var(--r3);
  padding: 20px;
  display: flex;
  align-items: flex-start;
  justify-content: space-between;
  transition: transform .15s, box-shadow .15s;
  cursor: default;
}

.stat-card:hover {
  transform: translateY(-2px);
  box-shadow: var(--e2);
}

.stat-card .stat-info h3 {
  font: 500 28px/36px 'Roboto', sans-serif;
  margin-bottom: 4px;
}

.stat-card .stat-info p {
  font: 400 13px/18px 'Roboto', sans-serif;
  color: var(--on-sfv);
}

.stat-card .stat-icon {
  width: 44px;
  height: 44px;
  border-radius: var(--r3);
  display: flex;
  align-items: center;
  justify-content: center;
  flex-shrink: 0;
}

.stat-card .stat-icon .mi {
  font-size: 24px;
}

.stat-icon.green {
  background: rgba(125,216,129,.12);
  color: var(--green);
}

.stat-icon.red {
  background: rgba(255,107,107,.12);
  color: var(--red);
}

.stat-icon.orange {
  background: rgba(255,158,67,.12);
  color: var(--orange);
}

.stat-icon.purple {
  background: rgba(200,191,255,.12);
  color: var(--pri);
}

.section-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  margin-bottom: 16px;
}

.section-header h3 {
  font: 500 16px/24px 'Roboto', sans-serif;
}

.section-header .link {
  font: 500 13px/18px 'Roboto', sans-serif;
  color: var(--pri);
  text-decoration: none;
  cursor: pointer;
  display: flex;
  align-items: center;
  gap: 4px;
}

.section-header .link:hover {
  text-decoration: underline;
}

.section-header .link .mi {
  font-size: 16px;
}

.quick-grid {
  display: grid;
  grid-template-columns: repeat(4, 1fr);
  gap: 12px;
  margin-bottom: 24px;
}

.quick-item {
  background: var(--sc);
  border-radius: var(--r3);
  padding: 16px;
  display: flex;
  flex-direction: column;
  gap: 10px;
  cursor: pointer;
  transition: transform .15s, background .15s, border-color .15s;
  border: 1px solid transparent;
  min-height: 154px;
}

.quick-item:hover {
  transform: translateY(-2px);
  background: var(--sc2);
}

.quick-item .qi-top {
  display: flex;
  align-items: center;
  justify-content: space-between;
}

.quick-item .qi-icon {
  width: 34px;
  height: 34px;
  border-radius: var(--r2);
  display: flex;
  align-items: center;
  justify-content: center;
  background: rgba(200,191,255,.12);
}

.quick-item .qi-icon .mi {
  font-size: 20px;
  color: var(--pri);
}

.quick-item .qi-pill {
  height: 22px;
  padding: 0 8px;
  border-radius: var(--r6);
  background: var(--sc2);
  font: 500 11px/22px 'Roboto', sans-serif;
  color: var(--on-sfv);
}

.quick-item .qi-title {
  font: 500 14px/20px 'Roboto', sans-serif;
}

.quick-item .qi-desc {
  font: 400 12px/17px 'Roboto', sans-serif;
  color: var(--on-sfv);
  min-height: 34px;
}

.quick-item .qi-foot {
  margin-top: auto;
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 8px;
}

.quick-item .qi-meta {
  font: 500 11px/16px 'Roboto', sans-serif;
  color: var(--ol);
}

.quick-item .qi-btn {
  height: 28px;
  padding: 0 10px;
  border-radius: var(--r6);
  border: 1px solid var(--olv);
  background: transparent;
  color: var(--on-sfv);
  font: 500 12px/16px 'Roboto', sans-serif;
  cursor: pointer;
}

.quick-item .qi-btn:hover {
  background: var(--sc3);
}

.dash-grid {
  display: grid;
  grid-template-columns: 1fr 360px;
  gap: 20px;
}

.camera-grid {
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(260px, 1fr));
  gap: 14px;
}

.cam-card {
  background: var(--sc);
  border-radius: var(--r3);
  overflow: hidden;
  cursor: pointer;
  transition: transform .15s, box-shadow .15s;
}

.cam-card:hover {
  transform: translateY(-2px);
  box-shadow: var(--e2);
}

.cam-thumb {
  width: 100%;
  aspect-ratio: 16/9;
  position: relative;
  overflow: hidden;
  background: linear-gradient(135deg, #0d0d15, #1c1728);
}

.cam-thumb .sv-lines {
  position: absolute;
  inset: 0;
  background: repeating-linear-gradient(0deg, transparent, transparent 2px, rgba(255,255,255,.01) 2px, rgba(255,255,255,.01) 4px);
  pointer-events: none;
}

.cam-thumb .sv-grid {
  position: absolute;
  inset: 0;
  background: linear-gradient(rgba(255,255,255,.02) 1px, transparent 1px), linear-gradient(90deg, rgba(255,255,255,.02) 1px, transparent 1px);
  background-size: 24px 24px;
  pointer-events: none;
}

.cam-thumb .sv-icon {
  position: absolute;
  inset: 0;
  display: flex;
  align-items: center;
  justify-content: center;
}

.cam-thumb .sv-icon .mi {
  font-size: 36px;
  color: rgba(255,255,255,.06);
}

.cam-thumb .scan {
  position: absolute;
  top: 0;
  left: 0;
  right: 0;
  height: 2px;
  background: rgba(200,191,255,.08);
  animation: scan 3s linear infinite;
}

.cam-thumb .live-badge {
  position: absolute;
  top: 8px;
  left: 8px;
  display: flex;
  align-items: center;
  gap: 4px;
  padding: 3px 8px;
  border-radius: var(--r1);
  background: rgba(200,60,60,.9);
  font: 500 10px/16px 'Roboto', sans-serif;
  color: #fff;
}

.cam-thumb .live-badge .dot {
  width: 5px;
  height: 5px;
  border-radius: 50%;
  background: #fff;
}

.cam-thumb .offline-badge {
  position: absolute;
  top: 8px;
  left: 8px;
  padding: 3px 8px;
  border-radius: var(--r1);
  background: rgba(80,80,80,.9);
  font: 500 10px/16px 'Roboto', sans-serif;
  color: #aaa;
}

.cam-thumb .ts {
  position: absolute;
  bottom: 6px;
  right: 6px;
  padding: 2px 6px;
  border-radius: var(--r1);
  background: rgba(0,0,0,.6);
  font: 500 10px/16px 'Roboto', sans-serif;
  color: #fff;
}

.cam-info {
  padding: 12px 14px;
}

.cam-info h4 {
  font: 500 14px/20px 'Roboto', sans-serif;
  margin-bottom: 3px;
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}

.cam-info .cam-meta {
  display: flex;
  align-items: center;
  justify-content: space-between;
}

.cam-info .cam-loc {
  display: flex;
  align-items: center;
  gap: 4px;
  font: 400 12px/16px 'Roboto', sans-serif;
  color: var(--on-sfv);
  min-width: 0;
}

.cam-info .cam-loc .mi {
  font-size: 14px;
  flex-shrink: 0;
}

.cam-info .cam-loc span {
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}

.cam-info .cam-status {
  display: flex;
  align-items: center;
  gap: 5px;
  font: 500 11px/16px 'Roboto', sans-serif;
  flex-shrink: 0;
  text-transform: capitalize;
}

.cam-info .cam-status .dot {
  width: 7px;
  height: 7px;
  border-radius: 50%;
}

.cam-info .cam-status .dot.on {
  background: var(--green);
}

.cam-info .cam-status .dot.off {
  background: var(--red);
}

.cam-info .cam-status .dot.rec {
  background: var(--orange);
}

.alert-panel {
  background: var(--sc);
  border-radius: var(--r3);
  padding: 0;
  display: flex;
  flex-direction: column;
  max-height: 600px;
}

.alert-panel .ap-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 16px 18px 12px;
  border-bottom: 1px solid var(--olv);
}

.alert-panel .ap-header h3 {
  font: 500 15px/22px 'Roboto', sans-serif;
}

.alert-panel .ap-header .link {
  font: 500 12px/16px 'Roboto', sans-serif;
  color: var(--pri);
  cursor: pointer;
  text-decoration: none;
}

.alert-list {
  flex: 1;
  overflow-y: auto;
  padding: 0 14px;
  scrollbar-width: none;
}

.alert-list::-webkit-scrollbar {
  display: none;
}

.alert-item {
  display: flex;
  gap: 12px;
  padding: 13px 0;
  border-bottom: 1px solid var(--olv);
}

.alert-item:last-child {
  border-bottom: none;
}

.alert-item .ai-icon {
  width: 36px;
  height: 36px;
  border-radius: 50%;
  display: flex;
  align-items: center;
  justify-content: center;
  flex-shrink: 0;
}

.alert-item .ai-icon.motion {
  background: rgba(255,158,67,.12);
  color: var(--orange);
}

.alert-item .ai-icon.alarm {
  background: rgba(255,107,107,.12);
  color: var(--red);
}

.alert-item .ai-icon.info {
  background: rgba(200,191,255,.12);
  color: var(--pri);
}

.alert-item .ai-icon .mi {
  font-size: 18px;
}

.alert-item .ai-content {
  flex: 1;
  min-width: 0;
}

.alert-item .ai-content h4 {
  font: 500 13px/18px 'Roboto', sans-serif;
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}

.alert-item .ai-content p {
  font: 400 12px/16px 'Roboto', sans-serif;
  color: var(--on-sfv);
  margin-top: 2px;
  display: -webkit-box;
  -webkit-box-orient: vertical;
  -webkit-line-clamp: 2;
  overflow: hidden;
}

.alert-item .ai-content .ai-time {
  font: 400 11px/16px 'Roboto', sans-serif;
  color: var(--ol);
  margin-top: 3px;
}

.loading-state,
.empty-state {
  text-align: center;
  padding: 60px 20px;
  color: var(--text-secondary);
  background: var(--sc);
  border: 1px solid var(--olv);
  border-radius: var(--r3);
}

.empty-state p {
  margin-bottom: 16px;
}

@media (max-width: 1439px) {
  .dash-grid {
    grid-template-columns: 1fr 300px;
  }

  .stats-row {
    grid-template-columns: repeat(2, 1fr);
  }

  .quick-grid {
    grid-template-columns: repeat(4, 1fr);
  }
}

@media (max-width: 1023px) {
  .dash-grid {
    grid-template-columns: 1fr;
  }

  .stats-row {
    grid-template-columns: repeat(2, 1fr);
  }
}

@media (max-width: 768px) {
  .stats-row {
    grid-template-columns: 1fr;
  }

  .quick-grid {
    grid-template-columns: repeat(2, 1fr);
  }

  .camera-grid {
    grid-template-columns: 1fr;
  }
}
</style>
