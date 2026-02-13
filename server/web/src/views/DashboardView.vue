<script setup>
import { ref, onMounted, onBeforeUnmount, computed } from 'vue'
import { useRouter } from 'vue-router'
import { useCameraStore } from '../stores/camera.js'
import { useDashboardStore } from '../stores/dashboard.js'
import { useAuthStore } from '../stores/auth.js'
import { connectSignaling } from '../api/signaling.js'

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

const streamingCount = computed(() => dashboardStore.stats.cameras?.streaming || 0)

onMounted(async () => {
  await cameraStore.fetchCameras()
  dashboardStore.fetchStats()
  dashboardStore.fetchRecentSessions()
  dashboardStore.fetchHealth()

  socket = connectSignaling(auth.token)
  dashboardStore.setSocketConnected(socket.connected)

  socket.on('connect', () => dashboardStore.setSocketConnected(true))
  socket.on('disconnect', () => dashboardStore.setSocketConnected(false))

  socket.emit('join-room', { room: `dashboard-${auth.user?.id}` })

  socket.on('camera-status', ({ cameraId, status }) => {
    cameraStore.updateCameraStatus(cameraId, status)
    dashboardStore.fetchStats()
    dashboardStore.fetchRecentSessions()
  })

  socket.on('activity-event', (event) => {
    dashboardStore.pushActivityEvent(event)
  })

  refreshInterval = setInterval(() => {
    dashboardStore.fetchStats()
    dashboardStore.fetchHealth()
  }, 30000)
})

onBeforeUnmount(() => {
  if (refreshInterval) {
    clearInterval(refreshInterval)
  }
  if (socket) {
    socket.off('camera-status')
    socket.off('activity-event')
    socket.off('connect')
    socket.off('disconnect')
  }
})

function handleWatch(camera) {
  router.push(`/watch/${camera.id}`)
}

function handleDetails(camera) {
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
  dashboardStore.fetchStats()
}

async function onCameraSaved() {
  editTarget.value = null
  await cameraStore.fetchCameras()
  dashboardStore.fetchStats()
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
            v-for="camera in cameraStore.cameras"
            :key="camera.id"
            :camera="camera"
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
