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
  }

  function setSocketConnected(connected) {
    socketConnected.value = connected
  }

  return {
    stats,
    statsLoading,
    activityEvents,
    recentSessions,
    sessionsLoading,
    health,
    socketConnected,
    fetchStats,
    fetchRecentSessions,
    fetchHealth,
    pushActivityEvent,
    setSocketConnected,
  }
})
