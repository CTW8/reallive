<script setup>
import { ref } from 'vue'

const props = defineProps({
  camera: { type: Object, required: true },
})

const emit = defineEmits(['edit', 'delete', 'watch'])

const showStreamKey = ref(false)

function statusClass(status) {
  return `status-badge status-${status || 'offline'}`
}

function copyStreamKey() {
  navigator.clipboard.writeText(props.camera.stream_key)
}
</script>

<template>
  <div class="camera-card">
    <div class="card-header">
      <div class="card-title">
        <h3>{{ camera.name }}</h3>
        <span :class="statusClass(camera.status)">{{ camera.status || 'offline' }}</span>
      </div>
      <div class="card-actions">
        <button class="btn btn-secondary btn-sm" @click="emit('edit', camera)">Edit</button>
        <button class="btn btn-danger btn-sm" @click="emit('delete', camera)">Delete</button>
      </div>
    </div>
    <div class="card-body">
      <div class="card-info">
        <div class="info-row">
          <span class="info-label">Resolution</span>
          <span class="info-value">{{ camera.resolution || '1080p' }}</span>
        </div>
        <div class="info-row">
          <span class="info-label">Stream Key</span>
          <div class="stream-key-row">
            <code class="info-value stream-key">{{ showStreamKey ? camera.stream_key : '****-****-****' }}</code>
            <button class="btn-icon" @click="showStreamKey = !showStreamKey" :title="showStreamKey ? 'Hide' : 'Show'">
              {{ showStreamKey ? '&#9660;' : '&#9654;' }}
            </button>
            <button v-if="showStreamKey" class="btn-icon" @click="copyStreamKey" title="Copy">&#9112;</button>
          </div>
        </div>
      </div>
    </div>
    <div class="card-footer">
      <button
        class="btn btn-primary btn-sm"
        :disabled="camera.status === 'offline'"
        @click="emit('watch', camera)"
      >
        Watch Live
      </button>
    </div>
  </div>
</template>

<style scoped>
.camera-card {
  background: var(--bg-card);
  border: 1px solid var(--border-color);
  border-radius: var(--radius-lg);
  overflow: hidden;
  transition: border-color 0.2s;
}

.camera-card:hover {
  border-color: var(--accent);
}

.card-header {
  display: flex;
  justify-content: space-between;
  align-items: flex-start;
  padding: 16px 20px 12px;
}

.card-title {
  display: flex;
  align-items: center;
  gap: 10px;
}

.card-title h3 {
  font-size: 1rem;
  font-weight: 600;
}

.card-actions {
  display: flex;
  gap: 6px;
}

.card-body {
  padding: 0 20px 12px;
}

.info-row {
  display: flex;
  justify-content: space-between;
  align-items: center;
  padding: 6px 0;
}

.info-label {
  font-size: 0.82rem;
  color: var(--text-secondary);
}

.info-value {
  font-size: 0.85rem;
  color: var(--text-primary);
}

.stream-key {
  font-family: 'SF Mono', 'Fira Code', monospace;
  font-size: 0.8rem;
  background: var(--bg-input);
  padding: 2px 8px;
  border-radius: 4px;
}

.stream-key-row {
  display: flex;
  align-items: center;
  gap: 6px;
}

.btn-icon {
  background: none;
  border: none;
  color: var(--text-secondary);
  font-size: 0.8rem;
  padding: 2px 4px;
  cursor: pointer;
}

.btn-icon:hover {
  color: var(--text-primary);
}

.card-footer {
  padding: 12px 20px 16px;
  border-top: 1px solid var(--border-color);
}
</style>
