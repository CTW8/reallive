<script setup>
import { ref, computed, onMounted, onBeforeUnmount, watch } from 'vue'
import mpegts from 'mpegts.js'

const props = defineProps({
  camera: { type: Object, required: true },
  eventState: {
    type: Object,
    default: () => ({ unread: 0, total: 0, lastTimestamp: null }),
  },
})

const emit = defineEmits(['watch', 'details', 'edit', 'delete'])

const videoRef = ref(null)
const cardRef = ref(null)
const isVisible = ref(false)

let player = null
let observer = null

const isStreaming = computed(() => props.camera.status === 'streaming')
const isOffline = computed(() => props.camera.status === 'offline' || !props.camera.status)

const flvUrl = computed(() => {
  return `http://${window.location.hostname}/live/${props.camera.stream_key}.flv`
})

const dotClass = computed(() => {
  if (props.camera.status === 'streaming') return 'status-dot dot-streaming'
  if (props.camera.status === 'online') return 'status-dot dot-online'
  return 'status-dot dot-offline'
})

const statusBadgeClass = computed(() => {
  return `status-badge status-${props.camera.status || 'offline'}`
})

const personUnread = computed(() => Number(props.eventState?.unread || 0))
const personTotal = computed(() => Number(props.eventState?.total || 0))

const personLastLabel = computed(() => {
  const raw = props.eventState?.lastTimestamp
  if (!raw) return ''
  const ts = new Date(raw).getTime()
  if (!Number.isFinite(ts)) return ''
  const diffSec = Math.max(0, Math.floor((Date.now() - ts) / 1000))
  if (diffSec < 60) return 'just now'
  const mins = Math.floor(diffSec / 60)
  if (mins < 60) return `${mins}m ago`
  const hours = Math.floor(mins / 60)
  if (hours < 24) return `${hours}h ago`
  return `${Math.floor(hours / 24)}d ago`
})

function createPlayer() {
  if (player || !videoRef.value || !isStreaming.value) return
  if (!mpegts.isSupported()) return

  player = mpegts.createPlayer({
    type: 'flv',
    url: flvUrl.value,
    isLive: true,
  }, {
    enableWorker: false,
    enableStashBuffer: false,
    stashInitialSize: 128,
    lazyLoad: false,
  })

  player.attachMediaElement(videoRef.value)
  player.load()
  player.play().catch(() => {})
}

function destroyPlayer() {
  if (!player) return
  try {
    player.pause()
    player.unload()
    player.detachMediaElement()
    player.destroy()
  } catch {
    // ignore cleanup errors
  }
  player = null
}

function setupObserver() {
  if (!cardRef.value) return

  observer = new IntersectionObserver(
    (entries) => {
      const entry = entries[0]
      isVisible.value = entry.isIntersecting

      if (entry.isIntersecting && isStreaming.value) {
        createPlayer()
      } else {
        destroyPlayer()
      }
    },
    { threshold: 0.1 }
  )

  observer.observe(cardRef.value)
}

watch(
  () => props.camera.status,
  (newStatus) => {
    if (newStatus === 'streaming' && isVisible.value) {
      createPlayer()
    } else {
      destroyPlayer()
    }
  }
)

onMounted(() => {
  setupObserver()
})

onBeforeUnmount(() => {
  destroyPlayer()
  if (observer) {
    observer.disconnect()
    observer = null
  }
})
</script>

<template>
  <div
    ref="cardRef"
    class="camera-card-enhanced"
    :class="{ 'card-streaming': isStreaming }"
  >
    <div class="preview-area">
      <video
        v-if="isStreaming"
        ref="videoRef"
        class="preview-video"
        muted
        autoplay
        playsinline
      />
      <img
        v-else-if="camera.thumbnailUrl"
        class="preview-thumbnail"
        :src="camera.thumbnailUrl"
        :alt="`${camera.name} thumbnail`"
        loading="lazy"
      />
      <div v-else class="preview-placeholder">
        <span class="placeholder-text">{{ camera.status || 'offline' }}</span>
      </div>
    </div>

    <div class="card-info">
      <div class="info-top">
        <div class="camera-identity">
          <span :class="dotClass" />
          <span class="camera-name">{{ camera.name }}</span>
        </div>
        <div class="info-badges">
          <span v-if="personTotal > 0" class="person-event-badge" :class="{ 'has-unread': personUnread > 0 }">
            Person {{ personUnread > 0 ? `+${personUnread}` : personTotal }}
          </span>
          <span :class="statusBadgeClass">{{ camera.status || 'offline' }}</span>
          <span v-if="camera.resolution" class="resolution-badge">{{ camera.resolution }}</span>
        </div>
      </div>
      <div v-if="personTotal > 0" class="event-meta">
        Last person event {{ personLastLabel }}
      </div>
    </div>

    <div class="card-actions">
      <button
        class="btn btn-primary btn-sm"
        @click="emit('watch', camera)"
      >
        Watch
      </button>
      <button
        class="btn btn-secondary btn-sm"
        @click="emit('details', camera)"
      >
        Details
      </button>
      <button
        class="btn btn-secondary btn-sm"
        @click="emit('edit', camera)"
      >
        Edit
      </button>
      <button
        class="btn btn-danger btn-sm"
        @click="emit('delete', camera)"
      >
        Delete
      </button>
    </div>
  </div>
</template>

<style scoped>
.camera-card-enhanced {
  background: var(--bg-card);
  border: 1px solid var(--border-color);
  border-radius: var(--radius-lg);
  overflow: hidden;
  transition: border-color 0.3s, box-shadow 0.3s;
}

.camera-card-enhanced:hover {
  border-color: var(--bg-hover);
}

.card-streaming {
  border-color: var(--accent);
  box-shadow: 0 0 12px rgba(76, 123, 244, 0.2);
}

.preview-area {
  position: relative;
  width: 100%;
  padding-top: 56.25%;
  background: var(--bg-primary);
  overflow: hidden;
}

.preview-video {
  position: absolute;
  top: 0;
  left: 0;
  width: 100%;
  height: 100%;
  object-fit: cover;
}

.preview-thumbnail {
  position: absolute;
  top: 0;
  left: 0;
  width: 100%;
  height: 100%;
  object-fit: cover;
  display: block;
}

.preview-placeholder {
  position: absolute;
  top: 0;
  left: 0;
  width: 100%;
  height: 100%;
  display: flex;
  align-items: center;
  justify-content: center;
  background: var(--bg-primary);
}

.placeholder-text {
  font-size: 0.85rem;
  color: var(--text-muted);
  text-transform: uppercase;
  letter-spacing: 1px;
  font-weight: 500;
}

.card-info {
  padding: 14px 16px 10px;
}

.info-top {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 10px;
}

.camera-identity {
  display: flex;
  align-items: center;
  gap: 8px;
  min-width: 0;
}

.status-dot {
  width: 8px;
  height: 8px;
  border-radius: 50%;
  flex-shrink: 0;
}

.camera-name {
  font-size: 0.95rem;
  font-weight: 600;
  color: var(--text-primary);
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}

.info-badges {
  display: flex;
  align-items: center;
  gap: 6px;
  flex-shrink: 0;
}

.resolution-badge {
  font-size: 0.72rem;
  font-weight: 600;
  color: var(--text-secondary);
  background: var(--bg-input);
  padding: 2px 8px;
  border-radius: 4px;
  text-transform: uppercase;
}

.person-event-badge {
  font-size: 0.7rem;
  font-weight: 700;
  color: #fca5a5;
  background: rgba(248, 113, 113, 0.16);
  border: 1px solid rgba(248, 113, 113, 0.3);
  padding: 2px 8px;
  border-radius: 999px;
  letter-spacing: 0.2px;
}

.person-event-badge.has-unread {
  color: #fff;
  background: rgba(239, 68, 68, 0.86);
  border-color: rgba(252, 165, 165, 0.65);
}

.card-actions {
  display: flex;
  gap: 6px;
  padding: 10px 16px 14px;
  border-top: 1px solid var(--border-color);
}

.card-actions .btn {
  flex: 1;
}

.event-meta {
  margin-top: 6px;
  color: var(--text-secondary);
  font-size: 0.76rem;
}

@keyframes pulse {
  0%, 100% { opacity: 1; }
  50% { opacity: 0.4; }
}

.dot-streaming {
  background: var(--accent);
  animation: pulse 2s ease-in-out infinite;
}

.dot-online {
  background: var(--success);
}

.dot-offline {
  background: var(--text-muted);
}
</style>
