<script setup>
import { ref, onMounted, onBeforeUnmount, computed } from 'vue'
import { useRouter } from 'vue-router'
import { useCameraStore } from '../stores/camera.js'
import { useDashboardStore } from '../stores/dashboard.js'
import { useAuthStore } from '../stores/auth.js'
import { connectSignaling } from '../api/signaling.js'
import { cameraApi } from '../api/index.js'

import DashboardHeader from '../components/dashboard/DashboardHeader.vue'
import StatsBar from '../components/dashboard/StatsBar.vue'
import SystemStatus from '../components/dashboard/SystemStatus.vue'
import ActivityFeed from '../components/dashboard/ActivityFeed.vue'
import SessionHistory from '../components/dashboard/SessionHistory.vue'
import CameraCardEnhanced from '../components/dashboard/CameraCardEnhanced.vue'
import AddCameraModal from '../components/dashboard/AddCameraModal.vue'
import EditCameraModal from '../components/dashboard/EditCameraModal.vue'
import CameraDetailModal from '../components/dashboard/CameraDetailModal.vue'

const router = useRouter()
const cameraStore = useCameraStore()
const dashboardStore = useDashboardStore()
const auth = useAuthStore()

const showAddModal = ref(false)
const editTarget = ref(null)
const detailTarget = ref(null)

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

function handleDetails(camera) {
  dashboardStore.clearCameraUnread(camera.id)
  detailTarget.value = camera
}

function handleEdit(camera) {
  editTarget.value = camera
}

function handleDelete(camera) {
  if (confirm(`Delete camera "${camera.name}"? This cannot be undone.`)) {
    cameraStore.removeCamera(camera.id)
    dashboardStore.fetchStats()
  }
}

async function onCameraAdded() {
  showAddModal.value = false
  await cameraStore.fetchCameras()
  await hydrateCameraEventCounts()
  dashboardStore.fetchStats()
}

async function onCameraSaved() {
  editTarget.value = null
  await cameraStore.fetchCameras()
  await hydrateCameraEventCounts()
  dashboardStore.fetchStats()
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
    } catch {
    }
  })
  await Promise.all(tasks)
}
</script>

<template>
  <div class="dashboard">
    <DashboardHeader
      :camera-count="cameraStore.cameras.length"
      @add-camera="showAddModal = true"
    />

    <StatsBar
      :stats="dashboardStore.stats"
      :loading="dashboardStore.statsLoading"
    />

    <div class="dashboard-body">
      <div class="dashboard-main">
        <div v-if="cameraStore.loading" class="loading-state">
          Loading cameras...
        </div>

        <div v-else-if="cameraStore.cameras.length === 0" class="empty-state">
          <p>No cameras configured yet.</p>
          <button class="btn btn-primary" @click="showAddModal = true">
            + Add Your First Camera
          </button>
        </div>

        <div v-else class="camera-grid">
          <CameraCardEnhanced
            v-for="camera in camerasWithEventState"
            :key="camera.id"
            :camera="camera"
            :event-state="camera.personEventState"
            @watch="handleWatch"
            @details="handleDetails"
            @edit="handleEdit"
            @delete="handleDelete"
          />
        </div>
      </div>

      <div class="dashboard-sidebar">
        <SystemStatus
          :health="dashboardStore.health"
          :socket-connected="dashboardStore.socketConnected"
          :streaming-count="streamingCount"
        />

        <ActivityFeed :events="dashboardStore.activityEvents" />

        <SessionHistory
          :sessions="dashboardStore.recentSessions"
          :loading="dashboardStore.sessionsLoading"
        />
      </div>
    </div>

    <AddCameraModal
      v-if="showAddModal"
      @close="showAddModal = false"
      @added="onCameraAdded"
    />

    <EditCameraModal
      v-if="editTarget"
      :camera="editTarget"
      @close="editTarget = null"
      @saved="onCameraSaved"
    />

    <CameraDetailModal
      v-if="detailTarget"
      :camera="detailTarget"
      @close="detailTarget = null"
    />
  </div>
</template>

<style scoped>
.dashboard {
  display: flex;
  flex-direction: column;
  gap: 24px;
}

.dashboard-body {
  display: grid;
  grid-template-columns: 1fr 380px;
  gap: 24px;
  align-items: start;
}

@media (max-width: 1100px) {
  .dashboard-body {
    grid-template-columns: 1fr;
  }
}

.dashboard-main {
  min-width: 0;
}

.dashboard-sidebar {
  display: flex;
  flex-direction: column;
  gap: 16px;
}

.camera-grid {
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(320px, 1fr));
  gap: 16px;
}

.loading-state,
.empty-state {
  text-align: center;
  padding: 60px 20px;
  color: var(--text-secondary);
  background: var(--bg-card);
  border: 1px solid var(--border-color);
  border-radius: var(--radius-lg);
}

.empty-state p {
  margin-bottom: 16px;
}
</style>
