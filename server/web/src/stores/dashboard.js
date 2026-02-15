import { defineStore } from 'pinia'
import { ref } from 'vue'
import { dashboardApi, sessionApi } from '../api/index.js'

export const useDashboardStore = defineStore('dashboard', () => {
  const stats = ref({
    cameras: { total: 0, online: 0, streaming: 0, offline: 0 },
    sessions: { total: 0, active: 0, today: 0 },
    system: { uptime: 0, timestamp: '' },
  })
  const statsLoading = ref(false)

  const activityEvents = ref([])
  const MAX_EVENTS = 50
  const cameraEventStates = ref({})

  const recentSessions = ref([])
  const sessionsLoading = ref(false)

  const health = ref({ status: 'unknown', uptime: 0 })
  const socketConnected = ref(false)

  async function fetchStats() {
    statsLoading.value = true
    try {
      stats.value = await dashboardApi.getStats()
    } catch {
      // keep previous stats on error
    } finally {
      statsLoading.value = false
    }
  }

  async function fetchRecentSessions() {
    sessionsLoading.value = true
    try {
      const data = await sessionApi.list(10, 0)
      recentSessions.value = data.sessions
    } catch {
      // keep previous sessions on error
    } finally {
      sessionsLoading.value = false
    }
  }

  async function fetchHealth() {
    try {
      health.value = await dashboardApi.getHealth()
    } catch {
      health.value = { status: 'error', uptime: 0 }
    }
  }

  function pushActivityEvent(event) {
    activityEvents.value.unshift(event)
    if (activityEvents.value.length > MAX_EVENTS) {
      activityEvents.value.pop()
    }

    if (event?.type === 'person-detected' && event?.cameraId != null) {
      const cameraId = Number(event.cameraId)
      if (!Number.isFinite(cameraId)) return
      const current = cameraEventStates.value[cameraId] || {
        unread: 0,
        total: 0,
        lastTimestamp: null,
      }
      cameraEventStates.value = {
        ...cameraEventStates.value,
        [cameraId]: {
          unread: current.unread + 1,
          total: current.total + 1,
          lastTimestamp: event.timestamp || new Date().toISOString(),
        },
      }
    }
  }

  function clearCameraUnread(cameraId) {
    const id = Number(cameraId)
    if (!Number.isFinite(id)) return
    const current = cameraEventStates.value[id]
    if (!current) return
    cameraEventStates.value = {
      ...cameraEventStates.value,
      [id]: {
        ...current,
        unread: 0,
      },
    }
  }

  function setCameraEventTotal(cameraId, total, lastTimestamp = null) {
    const id = Number(cameraId)
    if (!Number.isFinite(id)) return
    const safeTotal = Math.max(0, Number(total) || 0)
    const current = cameraEventStates.value[id] || {
      unread: 0,
      total: 0,
      lastTimestamp: null,
    }
    cameraEventStates.value = {
      ...cameraEventStates.value,
      [id]: {
        unread: current.unread,
        total: Math.max(current.total, safeTotal),
        lastTimestamp: current.lastTimestamp || lastTimestamp || null,
      },
    }
  }

  function setSocketConnected(connected) {
    socketConnected.value = connected
  }

  return {
    stats,
    statsLoading,
    activityEvents,
    cameraEventStates,
    recentSessions,
    sessionsLoading,
    health,
    socketConnected,
    fetchStats,
    fetchRecentSessions,
    fetchHealth,
    pushActivityEvent,
    clearCameraUnread,
    setCameraEventTotal,
    setSocketConnected,
  }
})
