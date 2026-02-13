<script setup>
defineProps({
  sessions: {
    type: Array,
    default: () => [],
  },
  loading: {
    type: Boolean,
    default: false,
  },
})

function formatDuration(seconds) {
  if (seconds == null || seconds <= 0) return '< 1m'

  const hours = Math.floor(seconds / 3600)
  const minutes = Math.floor((seconds % 3600) / 60)

  if (hours > 0) {
    return minutes > 0 ? `${hours}h ${minutes}m` : `${hours}h`
  }

  if (minutes > 0) return `${minutes}m`

  return '< 1m'
}

function formatStartTime(timestamp) {
  if (!timestamp) return '--'
  const d = new Date(timestamp)
  const month = String(d.getMonth() + 1).padStart(2, '0')
  const day = String(d.getDate()).padStart(2, '0')
  const hours = String(d.getHours()).padStart(2, '0')
  const minutes = String(d.getMinutes()).padStart(2, '0')
  return `${month}-${day} ${hours}:${minutes}`
}

function isActive(session) {
  return session.status === 'active' || (!session.end_time && !session.duration_seconds)
}
</script>

<template>
  <div class="panel">
    <h3 class="panel-title">Recent Sessions</h3>

    <div v-if="loading" class="empty-state">
      Loading sessions...
    </div>

    <div v-else-if="sessions.length === 0" class="empty-state">
      No sessions recorded
    </div>

    <div v-else class="session-list">
      <div v-for="session in sessions" :key="session.id" class="session-row">
        <div class="session-info">
          <span class="session-camera">{{ session.camera_name }}</span>
          <span class="session-time">{{ formatStartTime(session.start_time) }}</span>
        </div>
        <div class="session-duration">
          <span v-if="isActive(session)" class="active-badge">Active</span>
          <span v-else class="duration-text">{{ formatDuration(session.duration_seconds) }}</span>
        </div>
      </div>
    </div>
  </div>
</template>

<style scoped>
.panel {
  background: var(--bg-card);
  border: 1px solid var(--border-color);
  border-radius: var(--radius-lg);
  padding: 20px;
}

.panel-title {
  font-size: 1rem;
  font-weight: 600;
  margin-bottom: 16px;
  color: var(--text-primary);
}

.empty-state {
  color: var(--text-muted);
  font-size: 0.88rem;
  text-align: center;
  padding: 24px 0;
}

.session-list {
  display: flex;
  flex-direction: column;
  gap: 0;
}

.session-row {
  display: flex;
  justify-content: space-between;
  align-items: center;
  padding: 10px 0;
  border-bottom: 1px solid var(--border-color);
}

.session-row:last-child {
  border-bottom: none;
  padding-bottom: 0;
}

.session-row:first-child {
  padding-top: 0;
}

.session-info {
  display: flex;
  flex-direction: column;
  gap: 2px;
  min-width: 0;
}

.session-camera {
  font-size: 0.88rem;
  font-weight: 600;
  color: var(--text-primary);
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}

.session-time {
  font-size: 0.78rem;
  color: var(--text-muted);
  font-family: 'SF Mono', 'Fira Code', monospace;
}

.session-duration {
  flex-shrink: 0;
  margin-left: 12px;
}

.duration-text {
  font-size: 0.85rem;
  color: var(--text-secondary);
}

.active-badge {
  display: inline-block;
  padding: 3px 10px;
  border-radius: 20px;
  font-size: 0.75rem;
  font-weight: 600;
  text-transform: uppercase;
  letter-spacing: 0.5px;
  background: rgba(52, 211, 153, 0.15);
  color: var(--success);
}
</style>
