<script setup>
import { computed } from 'vue'

const props = defineProps({
  health: {
    type: Object,
    default: () => ({ status: 'unknown', uptime: 0, memoryUsage: 0 }),
  },
  socketConnected: {
    type: Boolean,
    default: false,
  },
  streamingCount: {
    type: Number,
    default: 0,
  },
})

const uptimeFormatted = computed(() => {
  const totalSeconds = props.health.uptime || 0
  const hours = Math.floor(totalSeconds / 3600)
  const minutes = Math.floor((totalSeconds % 3600) / 60)
  return `${hours}h ${minutes}m`
})

const serverOk = computed(() => props.health.status === 'ok')

const srsStatus = computed(() => {
  if (props.streamingCount > 0) return 'active'
  return 'unknown'
})
</script>

<template>
  <div class="panel">
    <h3 class="panel-title">System Status</h3>

    <div class="status-list">
      <div class="status-row">
        <div class="status-left">
          <span class="status-dot" :class="serverOk ? 'dot-green' : 'dot-red'"></span>
          <span class="status-label">Server</span>
        </div>
        <span class="status-value">
          <template v-if="serverOk">
            Up {{ uptimeFormatted }}
          </template>
          <template v-else>
            Unavailable
          </template>
        </span>
      </div>

      <div class="status-row">
        <div class="status-left">
          <span class="status-dot" :class="socketConnected ? 'dot-green' : 'dot-red'"></span>
          <span class="status-label">WebSocket</span>
        </div>
        <span class="status-value">{{ socketConnected ? 'Connected' : 'Disconnected' }}</span>
      </div>

      <div class="status-row">
        <div class="status-left">
          <span class="status-dot" :class="srsStatus === 'active' ? 'dot-green' : 'dot-gray'"></span>
          <span class="status-label">SRS Media Server</span>
        </div>
        <span class="status-value">
          <template v-if="srsStatus === 'active'">
            {{ streamingCount }} stream{{ streamingCount !== 1 ? 's' : '' }}
          </template>
          <template v-else>
            Unknown
          </template>
        </span>
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

.status-list {
  display: flex;
  flex-direction: column;
  gap: 0;
}

.status-row {
  display: flex;
  justify-content: space-between;
  align-items: center;
  padding: 10px 0;
  border-bottom: 1px solid var(--border-color);
}

.status-row:last-child {
  border-bottom: none;
  padding-bottom: 0;
}

.status-row:first-child {
  padding-top: 0;
}

.status-left {
  display: flex;
  align-items: center;
  gap: 10px;
}

.status-dot {
  width: 8px;
  height: 8px;
  border-radius: 50%;
  flex-shrink: 0;
}

.dot-green {
  background: var(--success);
  box-shadow: 0 0 6px rgba(52, 211, 153, 0.4);
}

.dot-red {
  background: var(--danger);
  box-shadow: 0 0 6px rgba(248, 113, 113, 0.4);
}

.dot-gray {
  background: var(--text-muted);
}

.status-label {
  font-size: 0.88rem;
  color: var(--text-primary);
}

.status-value {
  font-size: 0.85rem;
  color: var(--text-secondary);
  text-align: right;
}
</style>
