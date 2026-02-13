<script setup>
import { ref, computed } from 'vue'

const props = defineProps({
  camera: { type: Object, required: true },
})

const emit = defineEmits(['close'])

const showStreamKey = ref(false)
const copiedField = ref('')

const statusBadgeClass = computed(() => {
  return `status-badge status-${props.camera.status || 'offline'}`
})

const rtmpUrl = computed(() => {
  return `rtmp://${window.location.hostname}:1935/live/${props.camera.stream_key}`
})

const flvUrl = computed(() => {
  return `http://${window.location.hostname}/live/${props.camera.stream_key}.flv`
})

const maskedKey = computed(() => {
  if (showStreamKey.value) return props.camera.stream_key
  return '\u2022\u2022\u2022\u2022\u2022\u2022\u2022\u2022\u2022\u2022\u2022\u2022'
})

const formattedDate = computed(() => {
  if (!props.camera.created_at) return 'N/A'
  const d = new Date(props.camera.created_at)
  return d.toLocaleDateString('en-US', {
    year: 'numeric',
    month: 'short',
    day: 'numeric',
    hour: '2-digit',
    minute: '2-digit',
  })
})

let copyTimer = null

async function copyToClipboard(text, field) {
  try {
    await navigator.clipboard.writeText(text)
    copiedField.value = field
    clearTimeout(copyTimer)
    copyTimer = setTimeout(() => {
      copiedField.value = ''
    }, 1500)
  } catch {
    // clipboard access denied
  }
}

function onOverlayClick(e) {
  if (e.target === e.currentTarget) {
    emit('close')
  }
}
</script>

<template>
  <div class="modal-overlay" @click="onOverlayClick">
    <div class="modal-card">
      <button class="close-btn" @click="emit('close')">&times;</button>

      <h2 class="modal-title">{{ camera.name }}</h2>

      <div class="info-section">
        <div class="info-row">
          <span class="info-label">ID</span>
          <span class="info-value mono">{{ camera.id }}</span>
        </div>

        <div class="info-row">
          <span class="info-label">Status</span>
          <span :class="statusBadgeClass">{{ camera.status || 'offline' }}</span>
        </div>

        <div class="info-row">
          <span class="info-label">Resolution</span>
          <span class="info-value">{{ camera.resolution || 'N/A' }}</span>
        </div>

        <div class="info-row">
          <span class="info-label">Created</span>
          <span class="info-value">{{ formattedDate }}</span>
        </div>
      </div>

      <div class="url-section">
        <div class="url-group">
          <div class="url-header">
            <span class="url-label">Stream Key</span>
            <div class="url-actions">
              <button class="btn-inline" @click="showStreamKey = !showStreamKey">
                {{ showStreamKey ? 'Hide' : 'Show' }}
              </button>
              <button class="btn-inline" @click="copyToClipboard(camera.stream_key, 'streamKey')">
                {{ copiedField === 'streamKey' ? 'Copied!' : 'Copy' }}
              </button>
            </div>
          </div>
          <code class="url-value">{{ maskedKey }}</code>
        </div>

        <div class="url-group">
          <div class="url-header">
            <span class="url-label">RTMP Push URL</span>
            <button class="btn-inline" @click="copyToClipboard(rtmpUrl, 'rtmp')">
              {{ copiedField === 'rtmp' ? 'Copied!' : 'Copy' }}
            </button>
          </div>
          <code class="url-value">{{ rtmpUrl }}</code>
        </div>

        <div class="url-group">
          <div class="url-header">
            <span class="url-label">HTTP-FLV Pull URL</span>
            <button class="btn-inline" @click="copyToClipboard(flvUrl, 'flv')">
              {{ copiedField === 'flv' ? 'Copied!' : 'Copy' }}
            </button>
          </div>
          <code class="url-value">{{ flvUrl }}</code>
        </div>
      </div>
    </div>
  </div>
</template>

<style scoped>
.modal-overlay {
  position: fixed;
  inset: 0;
  background: rgba(0, 0, 0, 0.6);
  display: flex;
  align-items: center;
  justify-content: center;
  z-index: 200;
}

.modal-card {
  background: var(--bg-card);
  border: 1px solid var(--border-color);
  border-radius: var(--radius-lg);
  padding: 32px;
  width: 100%;
  max-width: 520px;
  max-height: 90vh;
  overflow-y: auto;
  position: relative;
}

.close-btn {
  position: absolute;
  top: 16px;
  right: 16px;
  background: none;
  border: none;
  color: var(--text-secondary);
  font-size: 1.5rem;
  line-height: 1;
  padding: 4px 8px;
  border-radius: var(--radius);
  transition: color 0.2s, background 0.2s;
}

.close-btn:hover {
  color: var(--text-primary);
  background: var(--bg-hover);
}

.modal-title {
  font-size: 1.25rem;
  font-weight: 700;
  color: var(--text-primary);
  margin-bottom: 24px;
  padding-right: 32px;
}

.info-section {
  margin-bottom: 24px;
}

.info-row {
  display: flex;
  justify-content: space-between;
  align-items: center;
  padding: 10px 0;
  border-bottom: 1px solid var(--border-color);
}

.info-row:last-child {
  border-bottom: none;
}

.info-label {
  font-size: 0.85rem;
  color: var(--text-secondary);
  font-weight: 500;
}

.info-value {
  font-size: 0.9rem;
  color: var(--text-primary);
}

.info-value.mono {
  font-family: 'SF Mono', 'Fira Code', monospace;
  font-size: 0.82rem;
}

.url-section {
  display: flex;
  flex-direction: column;
  gap: 16px;
}

.url-group {
  background: var(--bg-secondary);
  border: 1px solid var(--border-color);
  border-radius: var(--radius);
  padding: 12px 14px;
}

.url-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 8px;
}

.url-label {
  font-size: 0.8rem;
  color: var(--text-secondary);
  font-weight: 500;
  text-transform: uppercase;
  letter-spacing: 0.3px;
}

.url-actions {
  display: flex;
  gap: 8px;
}

.btn-inline {
  background: none;
  border: none;
  color: var(--accent);
  font-size: 0.78rem;
  font-weight: 500;
  padding: 2px 6px;
  border-radius: 4px;
  transition: color 0.2s, background 0.2s;
}

.btn-inline:hover {
  color: var(--accent-hover);
  background: rgba(76, 123, 244, 0.1);
}

.url-value {
  display: block;
  font-family: 'SF Mono', 'Fira Code', monospace;
  font-size: 0.8rem;
  color: var(--text-primary);
  word-break: break-all;
  line-height: 1.5;
}
</style>
