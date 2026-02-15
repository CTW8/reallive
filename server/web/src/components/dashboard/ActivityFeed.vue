<script setup>
defineProps({
  events: {
    type: Array,
    default: () => [],
  },
})

function timeAgo(timestamp) {
  const now = Date.now()
  const then = new Date(timestamp).getTime()
  const diffMs = now - then

  if (diffMs < 0) return 'just now'

  const seconds = Math.floor(diffMs / 1000)
  if (seconds < 60) return 'just now'

  const minutes = Math.floor(seconds / 60)
  if (minutes < 60) return `${minutes}m ago`

  const hours = Math.floor(minutes / 60)
  if (hours < 24) return `${hours}h ago`

  const days = Math.floor(hours / 24)
  return `${days}d ago`
}

function eventDescription(type) {
  switch (type) {
    case 'stream-start':
      return 'Started streaming'
    case 'stream-stop':
      return 'Stopped streaming'
    case 'person-detected':
      return 'Person detected'
    default:
      return type
  }
}

function formatScore(value) {
  const n = Number(value)
  if (!Number.isFinite(n)) return ''
  const pct = Math.round(Math.max(0, Math.min(1, n)) * 100)
  return ` (${pct}%)`
}

function dotClass(type) {
  if (type === 'stream-start') return 'event-dot dot-green'
  if (type === 'stream-stop') return 'event-dot dot-red'
  if (type === 'person-detected') return 'event-dot dot-orange'
  return 'event-dot dot-gray'
}
</script>

<template>
  <div class="panel">
    <h3 class="panel-title">Activity</h3>

    <div v-if="events.length === 0" class="empty-state">
      No recent activity
    </div>

    <div v-else class="event-list">
      <div v-for="(event, index) in events" :key="index" class="event-row">
        <span :class="dotClass(event.type)"></span>
        <div class="event-content">
          <div class="event-header">
            <span class="event-camera">{{ event.cameraName }}</span>
            <span class="event-time">{{ timeAgo(event.timestamp) }}</span>
          </div>
          <span class="event-desc">{{ eventDescription(event.type) }}{{ event.type === 'person-detected' ? formatScore(event.score) : '' }}</span>
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

.event-list {
  max-height: 300px;
  overflow-y: auto;
  display: flex;
  flex-direction: column;
  gap: 0;
}

.event-list::-webkit-scrollbar {
  width: 4px;
}

.event-list::-webkit-scrollbar-track {
  background: transparent;
}

.event-list::-webkit-scrollbar-thumb {
  background: var(--border-color);
  border-radius: 2px;
}

.event-row {
  display: flex;
  align-items: flex-start;
  gap: 12px;
  padding: 10px 0;
  border-bottom: 1px solid var(--border-color);
}

.event-row:last-child {
  border-bottom: none;
  padding-bottom: 0;
}

.event-row:first-child {
  padding-top: 0;
}

.event-dot {
  width: 8px;
  height: 8px;
  border-radius: 50%;
  flex-shrink: 0;
  margin-top: 6px;
}

.dot-green {
  background: var(--success);
  box-shadow: 0 0 6px rgba(52, 211, 153, 0.4);
}

.dot-red {
  background: var(--danger);
  box-shadow: 0 0 6px rgba(248, 113, 113, 0.4);
}

.dot-orange {
  background: #f59e0b;
  box-shadow: 0 0 6px rgba(245, 158, 11, 0.45);
}

.dot-gray {
  background: var(--text-muted);
}

.event-content {
  flex: 1;
  min-width: 0;
}

.event-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  gap: 8px;
}

.event-camera {
  font-size: 0.88rem;
  font-weight: 600;
  color: var(--text-primary);
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}

.event-time {
  font-size: 0.78rem;
  color: var(--text-muted);
  flex-shrink: 0;
}

.event-desc {
  font-size: 0.82rem;
  color: var(--text-secondary);
  margin-top: 2px;
}
</style>
