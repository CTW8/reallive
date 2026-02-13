<script setup>
defineProps({
  stats: {
    type: Object,
    default: () => ({
      cameras: { total: 0, online: 0, streaming: 0, offline: 0 },
      sessions: { total: 0, active: 0, today: 0 },
    }),
  },
  loading: {
    type: Boolean,
    default: false,
  },
})
</script>

<template>
  <div class="stats-bar">
    <div class="stat-card">
      <div class="stat-icon icon-blue">
        <span>&#9673;</span>
      </div>
      <div class="stat-content">
        <span class="stat-value">{{ loading ? '--' : stats.cameras.total }}</span>
        <span class="stat-subtitle">Total Cameras</span>
      </div>
    </div>

    <div class="stat-card">
      <div class="stat-icon icon-green">
        <span>&#9679;</span>
      </div>
      <div class="stat-content">
        <span class="stat-value">{{ loading ? '--' : stats.cameras.online + stats.cameras.streaming }}</span>
        <span class="stat-subtitle">Online</span>
      </div>
    </div>

    <div class="stat-card">
      <div class="stat-icon icon-blue-pulse">
        <span>&#9655;</span>
      </div>
      <div class="stat-content">
        <span class="stat-value">{{ loading ? '--' : stats.cameras.streaming }}</span>
        <span class="stat-subtitle">Streaming</span>
      </div>
    </div>

    <div class="stat-card">
      <div class="stat-icon icon-yellow">
        <span>&#9670;</span>
      </div>
      <div class="stat-content">
        <span class="stat-value">{{ loading ? '--' : stats.sessions.today }}</span>
        <span class="stat-subtitle">Today Sessions</span>
      </div>
    </div>
  </div>
</template>

<style scoped>
.stats-bar {
  display: grid;
  grid-template-columns: repeat(4, 1fr);
  gap: 16px;
}

@media (max-width: 900px) {
  .stats-bar {
    grid-template-columns: repeat(2, 1fr);
  }
}

.stat-card {
  background: var(--bg-card);
  border: 1px solid var(--border-color);
  border-radius: var(--radius-lg);
  padding: 20px;
  display: flex;
  align-items: center;
  gap: 16px;
  transition: border-color 0.2s;
}

.stat-card:hover {
  border-color: var(--bg-hover);
}

.stat-icon {
  width: 48px;
  height: 48px;
  border-radius: var(--radius);
  display: flex;
  align-items: center;
  justify-content: center;
  font-size: 1.25rem;
  flex-shrink: 0;
}

.icon-blue {
  background: rgba(76, 123, 244, 0.15);
  color: var(--accent);
}

.icon-green {
  background: rgba(52, 211, 153, 0.15);
  color: var(--success);
}

.icon-blue-pulse {
  background: rgba(76, 123, 244, 0.15);
  color: var(--accent);
  animation: pulse 2s ease-in-out infinite;
}

@keyframes pulse {
  0%, 100% {
    opacity: 1;
  }
  50% {
    opacity: 0.6;
  }
}

.icon-yellow {
  background: rgba(251, 191, 36, 0.15);
  color: var(--warning);
}

.stat-content {
  display: flex;
  flex-direction: column;
  min-width: 0;
}

.stat-value {
  font-size: 1.5rem;
  font-weight: 700;
  line-height: 1.2;
  color: var(--text-primary);
}

.stat-subtitle {
  font-size: 0.82rem;
  color: var(--text-secondary);
  margin-top: 2px;
}
</style>
