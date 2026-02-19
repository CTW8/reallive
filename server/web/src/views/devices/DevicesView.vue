<script setup>
import { computed, onBeforeUnmount, onMounted, ref } from 'vue'
import { useRouter } from 'vue-router'
import { useCameraStore } from '../../stores/camera.js'
import { heartbeatClassFromLabel, heartbeatLabelFromTs, heartbeatTsFromRuntime } from '../../utils/heartbeat.js'

const router = useRouter()
const cameraStore = useCameraStore()

const viewMode = ref('table')
const activeFilter = ref('all')
const loading = ref(false)
const error = ref('')
const selectedIds = ref([])

const showModal = ref(false)
const editingCameraId = ref(null)
const formName = ref('')
const formResolution = ref('1080p')
const formStatus = ref('offline')
const formError = ref('')
const submitting = ref(false)
const latestCreatedToken = ref('')
const latestCreatedCameraName = ref('')
const latestCreatedPushUrl = ref('')
const latestCreatedPullUrl = ref('')
const copiedToken = ref('')
const failedThumbnailByCamera = ref({})

let refreshTimer = null
let copyTokenTimer = null

const allCameras = computed(() => cameraStore.cameras || [])

const normalizedCameras = computed(() => {
  return allCameras.value.map((cam) => {
    const raw = String(cam.status || 'offline').toLowerCase()
    const status = raw === 'streaming' ? 'online' : raw
    const isRecording = raw === 'recording' || raw === 'streaming'
    return {
      ...cam,
      status,
      isRecording,
      stream_urls: cam.stream_urls || buildUrlsFromStreamKey(cam.stream_key),
      model: String(cam.resolution || '1080p'),
      location: 'Not configured',
      firmware: 'v2.4.1',
    }
  })
})

const stats = computed(() => {
  let online = 0
  let offline = 0
  let recording = 0
  for (const cam of normalizedCameras.value) {
    if (cam.status === 'offline') offline += 1
    else online += 1
    if (cam.isRecording) recording += 1
  }
  return {
    all: normalizedCameras.value.length,
    online,
    offline,
    recording,
    updates: normalizedCameras.value.filter((cam, idx) => idx % 4 === 0).length,
  }
})

const filterChips = computed(() => ([
  { key: 'all', label: 'All', count: stats.value.all },
  { key: 'online', label: 'Online', count: stats.value.online },
  { key: 'offline', label: 'Offline', count: stats.value.offline },
  { key: 'recording', label: 'Recording', count: stats.value.recording },
]))

const filteredCameras = computed(() => {
  if (activeFilter.value === 'all') return normalizedCameras.value
  if (activeFilter.value === 'recording') return normalizedCameras.value.filter((cam) => cam.isRecording)
  return normalizedCameras.value.filter((cam) => cam.status === activeFilter.value)
})

const allFilteredSelected = computed(() => {
  if (!filteredCameras.value.length) return false
  const set = new Set(selectedIds.value)
  return filteredCameras.value.every((cam) => set.has(cam.id))
})

const hasSelection = computed(() => selectedIds.value.length > 0)
const editingCamera = computed(() => {
  if (editingCameraId.value == null) return null
  return normalizedCameras.value.find((cam) => Number(cam.id) === Number(editingCameraId.value)) || null
})

onMounted(async () => {
  await refreshCameras()
  refreshTimer = setInterval(() => {
    void refreshCameras(false)
  }, 5000)
})

onBeforeUnmount(() => {
  if (refreshTimer) {
    clearInterval(refreshTimer)
    refreshTimer = null
  }
  if (copyTokenTimer) {
    clearTimeout(copyTokenTimer)
    copyTokenTimer = null
  }
})

async function refreshCameras(withLoading = true) {
  if (withLoading) loading.value = true
  error.value = ''
  try {
    await cameraStore.fetchCameras()
  } catch (err) {
    error.value = err?.message || 'Failed to load devices'
  } finally {
    if (withLoading) loading.value = false
  }
}

function toggleSelectAll(event) {
  const checked = Boolean(event?.target?.checked)
  const set = new Set(selectedIds.value)
  if (checked) {
    for (const cam of filteredCameras.value) set.add(cam.id)
  } else {
    for (const cam of filteredCameras.value) set.delete(cam.id)
  }
  selectedIds.value = [...set]
}

function toggleSelection(id, checked) {
  const set = new Set(selectedIds.value)
  if (checked) set.add(id)
  else set.delete(id)
  selectedIds.value = [...set]
}

function openCreateModal() {
  editingCameraId.value = null
  formName.value = ''
  formResolution.value = '1080p'
  formStatus.value = 'offline'
  formError.value = ''
  showModal.value = true
}

function openEditModal(camera) {
  editingCameraId.value = camera.id
  formName.value = camera.name || ''
  formResolution.value = camera.resolution || '1080p'
  formStatus.value = camera.status || 'offline'
  formError.value = ''
  showModal.value = true
}

function closeModal() {
  if (submitting.value) return
  showModal.value = false
}

async function saveModal() {
  const name = formName.value.trim()
  if (!name) {
    formError.value = 'Device name is required'
    return
  }

  submitting.value = true
  formError.value = ''
  try {
    if (editingCameraId.value == null) {
      const created = await cameraStore.addCamera(name, formResolution.value)
      const createdCamera = created?.camera || created || null
      const createdCameraId = Number(createdCamera?.id || 0)
      latestCreatedToken.value = String(createdCamera?.stream_key || '')
      latestCreatedCameraName.value = String(createdCamera?.name || name)
      latestCreatedPushUrl.value = String(createdCamera?.stream_urls?.push || buildUrlsFromStreamKey(createdCamera?.stream_key).push || '')
      latestCreatedPullUrl.value = String(createdCamera?.stream_urls?.pull_flv || buildUrlsFromStreamKey(createdCamera?.stream_key).pull_flv || '')
      if (createdCameraId && formStatus.value !== 'offline') {
        await cameraStore.updateCamera(createdCameraId, { status: formStatus.value })
      }
    } else {
      await cameraStore.updateCamera(editingCameraId.value, {
        name,
        resolution: formResolution.value,
        status: formStatus.value,
      })
    }
    showModal.value = false
    await refreshCameras(false)
  } catch (err) {
    formError.value = err?.message || 'Save failed'
  } finally {
    submitting.value = false
  }
}

async function deleteCamera(id) {
  if (!window.confirm('Delete this device?')) return
  try {
    await cameraStore.removeCamera(id)
    selectedIds.value = selectedIds.value.filter((v) => v !== id)
    if (Number(editingCameraId.value) === Number(id)) {
      showModal.value = false
      editingCameraId.value = null
    }
  } catch (err) {
    error.value = err?.message || 'Delete failed'
  }
}

async function deleteSelected() {
  if (!selectedIds.value.length) return
  if (!window.confirm(`Delete ${selectedIds.value.length} selected device(s)?`)) return
  const ids = [...selectedIds.value]
  for (const id of ids) {
    try {
      await cameraStore.removeCamera(id)
    } catch {
    }
  }
  selectedIds.value = []
  await refreshCameras(false)
}

function goLive(camera) {
  router.push(`/watch/${camera.id}`)
}

function goPlayback(camera = null) {
  const cameraId = Number(camera?.id || selectedIds.value[0] || 0)
  if (cameraId) {
    router.push(`/playback?cameraId=${cameraId}`)
    return
  }
  router.push('/playback')
}

function statusLabel(camera) {
  if (camera.isRecording) return 'Recording'
  if (camera.status === 'offline') return 'Offline'
  return 'Online'
}

function statusDotClass(camera) {
  if (camera.isRecording) return 'rec'
  if (camera.status === 'offline') return 'off'
  return 'on'
}

function statusBadgeClass(camera) {
  if (camera.isRecording) return 'recording'
  if (camera.status === 'offline') return 'offline'
  return 'online'
}

function heartbeatLabel(camera) {
  return heartbeatLabelFromTs(heartbeatTsFromRuntime(camera?.device || null))
}

function heartbeatClass(camera) {
  return heartbeatClassFromLabel(heartbeatLabel(camera))
}

function thumbnailVisible(camera) {
  if (!camera?.thumbnailUrl) return false
  return failedThumbnailByCamera.value[camera.id] !== camera.thumbnailUrl
}

function onThumbnailError(camera) {
  if (!camera?.id || !camera?.thumbnailUrl) return
  failedThumbnailByCamera.value[camera.id] = camera.thumbnailUrl
}

function onThumbnailLoad(camera) {
  if (!camera?.id) return
  if (failedThumbnailByCamera.value[camera.id]) {
    delete failedThumbnailByCamera.value[camera.id]
  }
}

function displayToken(token) {
  const value = String(token || '')
  if (!value) return '-'
  if (value.length <= 16) return value
  return `${value.slice(0, 8)}...${value.slice(-8)}`
}

function clearLatestToken() {
  latestCreatedToken.value = ''
  latestCreatedCameraName.value = ''
  latestCreatedPushUrl.value = ''
  latestCreatedPullUrl.value = ''
}

async function copyToken(token) {
  const value = String(token || '')
  if (!value) return
  try {
    if (navigator?.clipboard?.writeText) {
      await navigator.clipboard.writeText(value)
    } else {
      const textarea = document.createElement('textarea')
      textarea.value = value
      textarea.style.position = 'fixed'
      textarea.style.opacity = '0'
      document.body.appendChild(textarea)
      textarea.select()
      document.execCommand('copy')
      document.body.removeChild(textarea)
    }
    copiedToken.value = value
    if (copyTokenTimer) clearTimeout(copyTokenTimer)
    copyTokenTimer = setTimeout(() => {
      copiedToken.value = ''
      copyTokenTimer = null
    }, 1500)
  } catch {
  }
}

function getPushUrl(camera) {
  return String(camera?.stream_urls?.push || buildUrlsFromStreamKey(camera?.stream_key).push || '')
}

function getPullFlvUrl(camera) {
  return String(camera?.stream_urls?.pull_flv || buildUrlsFromStreamKey(camera?.stream_key).pull_flv || '')
}

function buildUrlsFromStreamKey(streamKey) {
  const key = String(streamKey || '').trim()
  if (!key) {
    return { push: '', pull_flv: '', pull_hls: '' }
  }
  const protocol = window?.location?.protocol === 'https:' ? 'https' : 'http'
  const hostWithPort = window?.location?.host || 'localhost'
  const host = window?.location?.hostname || 'localhost'
  return {
    push: `rtmp://${host}:1935/live/${key}`,
    pull_flv: `${protocol}://${hostWithPort}/live/${key}.flv`,
    pull_hls: `${protocol}://${hostWithPort}/live/${key}.m3u8`,
  }
}
</script>

<template>
  <div class="devices-page">
    <div class="device-stats-row">
      <div class="alert-stat-mini">
        <div class="asm-icon" style="background:rgba(125,216,129,.12);color:var(--green)"><span class="mi">videocam</span></div>
        <div class="asm-info"><h4>{{ stats.online }}</h4><p>Online</p></div>
      </div>
      <div class="alert-stat-mini">
        <div class="asm-icon" style="background:rgba(255,107,107,.12);color:var(--red)"><span class="mi">videocam_off</span></div>
        <div class="asm-info"><h4>{{ stats.offline }}</h4><p>Offline</p></div>
      </div>
      <div class="alert-stat-mini">
        <div class="asm-icon" style="background:rgba(255,158,67,.12);color:var(--orange)"><span class="mi">fiber_manual_record</span></div>
        <div class="asm-info"><h4>{{ stats.recording }}</h4><p>Recording</p></div>
      </div>
      <div class="alert-stat-mini">
        <div class="asm-icon" style="background:rgba(200,191,255,.12);color:var(--pri)"><span class="mi">system_update</span></div>
        <div class="asm-info"><h4>{{ stats.updates }}</h4><p>Update Available</p></div>
      </div>
    </div>

    <div class="device-toolbar">
      <div class="dt-left">
        <div class="filter-chips">
          <button
            v-for="chip in filterChips"
            :key="chip.key"
            class="filter-chip"
            :class="{ active: activeFilter === chip.key }"
            @click="activeFilter = chip.key"
          >
            {{ chip.label }} <span class="chip-count">{{ chip.count }}</span>
          </button>
        </div>
      </div>
      <div class="dt-right">
        <div class="view-toggle">
          <button class="vt-btn" :class="{ active: viewMode === 'table' }" @click="viewMode = 'table'" title="Table View"><span class="mi">view_list</span></button>
          <button class="vt-btn" :class="{ active: viewMode === 'card' }" @click="viewMode = 'card'" title="Card View"><span class="mi">grid_view</span></button>
        </div>
        <button class="mt-btn" :disabled="!hasSelection" @click="deleteSelected"><span class="mi">delete</span> Delete Selected</button>
        <button class="mt-btn primary" @click="openCreateModal"><span class="mi">add</span> Add Device</button>
      </div>
    </div>
    <div v-if="latestCreatedToken" class="token-banner">
      <div class="token-banner-left">
        <span class="mi">key</span>
        <div>
          <h4>Push token created for {{ latestCreatedCameraName || 'camera' }}</h4>
          <p>{{ latestCreatedToken }}</p>
          <p v-if="latestCreatedPushUrl"><strong>Push:</strong> {{ latestCreatedPushUrl }}</p>
          <p v-if="latestCreatedPullUrl"><strong>Pull:</strong> {{ latestCreatedPullUrl }}</p>
        </div>
      </div>
      <div class="token-banner-right">
        <button class="mt-btn" @click="copyToken(latestCreatedToken)">
          <span class="mi">{{ copiedToken === latestCreatedToken ? 'check' : 'content_copy' }}</span>
          {{ copiedToken === latestCreatedToken ? 'Copied' : 'Copy Token' }}
        </button>
        <button v-if="latestCreatedPushUrl" class="mt-btn" @click="copyToken(latestCreatedPushUrl)">
          <span class="mi">{{ copiedToken === latestCreatedPushUrl ? 'check' : 'content_copy' }}</span>
          {{ copiedToken === latestCreatedPushUrl ? 'Copied' : 'Copy Push URL' }}
        </button>
        <button v-if="latestCreatedPullUrl" class="mt-btn" @click="copyToken(latestCreatedPullUrl)">
          <span class="mi">{{ copiedToken === latestCreatedPullUrl ? 'check' : 'content_copy' }}</span>
          {{ copiedToken === latestCreatedPullUrl ? 'Copied' : 'Copy Pull URL' }}
        </button>
        <button class="mt-btn" @click="clearLatestToken">Dismiss</button>
      </div>
    </div>

    <div v-if="loading" class="panel-state">Loading devices...</div>
    <div v-else-if="error" class="panel-state error">{{ error }}</div>

    <template v-else>
      <div v-if="viewMode === 'table'" class="device-table-wrap">
        <div class="device-table">
          <div class="dt-head">
            <div><input type="checkbox" :checked="allFilteredSelected" @change="toggleSelectAll"></div>
            <div class="sortable">Device <span class="mi">unfold_more</span></div>
            <div>Status</div>
            <div>Resolution</div>
            <div>Push Token / URLs</div>
            <div>Storage</div>
            <div>Firmware</div>
            <div>Actions</div>
          </div>

          <div v-if="!filteredCameras.length" class="panel-state">No device in current filter.</div>

          <div v-for="camera in filteredCameras" :key="camera.id" class="dt-row">
            <div class="dt-cb"><input type="checkbox" :checked="selectedIds.includes(camera.id)" @change="toggleSelection(camera.id, $event.target.checked)"></div>
            <div class="dt-device">
              <div class="dev-thumb">
                <img
                  v-if="thumbnailVisible(camera)"
                  :src="camera.thumbnailUrl"
                  alt=""
                  @error="onThumbnailError(camera)"
                  @load="onThumbnailLoad(camera)"
                />
                <span v-else class="mi">videocam</span>
              </div>
              <div class="dev-info">
                <h4>{{ camera.name }}</h4>
                <p>{{ camera.location }}</p>
              </div>
            </div>
            <div class="dt-status">
              <span class="dot" :class="statusDotClass(camera)"></span>
              {{ statusLabel(camera) }}
              <span class="hb-chip" :class="heartbeatClass(camera)">{{ heartbeatLabel(camera) }}</span>
            </div>
            <div class="dt-cell">{{ camera.resolution || '1080p' }}</div>
            <div class="dt-cell token-cell">
              <span class="token-text" :title="camera.stream_key">{{ displayToken(camera.stream_key) }}</span>
              <button class="tiny-copy-btn" :disabled="!camera.stream_key" @click="copyToken(camera.stream_key)">
                <span class="mi">{{ copiedToken === camera.stream_key ? 'check' : 'content_copy' }}</span>
              </button>
              <button class="tiny-copy-btn" :disabled="!getPushUrl(camera)" :title="getPushUrl(camera)" @click="copyToken(getPushUrl(camera))">
                <span class="mi">{{ copiedToken === getPushUrl(camera) ? 'check' : 'upload' }}</span>
              </button>
              <button class="tiny-copy-btn" :disabled="!getPullFlvUrl(camera)" :title="getPullFlvUrl(camera)" @click="copyToken(getPullFlvUrl(camera))">
                <span class="mi">{{ copiedToken === getPullFlvUrl(camera) ? 'check' : 'download' }}</span>
              </button>
            </div>
            <div class="dt-cell">Local</div>
            <div class="dt-cell">{{ camera.firmware }}</div>
            <div class="dt-actions">
              <button class="act-btn" title="Settings" @click="openEditModal(camera)"><span class="mi">settings</span></button>
              <button class="act-btn" title="Live View" @click="goLive(camera)"><span class="mi">videocam</span></button>
              <button class="act-btn" title="Playback" @click="goPlayback(camera)"><span class="mi">history</span></button>
              <button class="act-btn" title="Delete" @click="deleteCamera(camera.id)"><span class="mi">delete</span></button>
            </div>
          </div>
        </div>
      </div>

      <div v-else class="device-card-grid">
        <div v-for="camera in filteredCameras" :key="camera.id" class="dev-card">
          <div class="dc-thumb">
            <img
              v-if="thumbnailVisible(camera)"
              :src="camera.thumbnailUrl"
              alt=""
              @error="onThumbnailError(camera)"
              @load="onThumbnailLoad(camera)"
            />
            <template v-else>
              <div class="sv-grid"></div>
              <span class="mi">videocam</span>
            </template>
            <div class="dc-status-badge" :class="statusBadgeClass(camera)">
              <span class="dot"></span>
              {{ statusLabel(camera) }}
            </div>
            <div class="dc-hb-badge" :class="heartbeatClass(camera)">{{ heartbeatLabel(camera) }}</div>
          </div>
          <div class="dc-body">
            <h4>{{ camera.name }}</h4>
            <div class="dc-meta">
              <div class="dc-meta-row"><span class="mi">location_on</span><span>{{ camera.location }}</span></div>
              <div class="dc-meta-row"><span class="mi">high_quality</span><span>{{ camera.resolution || '1080p' }}</span></div>
              <div class="dc-meta-row token-row">
                <span class="mi">key</span>
                <span class="token-text" :title="camera.stream_key">{{ displayToken(camera.stream_key) }}</span>
                <button class="tiny-copy-btn" :disabled="!camera.stream_key" @click="copyToken(camera.stream_key)">
                  <span class="mi">{{ copiedToken === camera.stream_key ? 'check' : 'content_copy' }}</span>
                </button>
              </div>
            </div>
          </div>
          <div class="dc-footer">
            <button class="act-btn" title="Live View" @click="goLive(camera)"><span class="mi">videocam</span></button>
            <button class="act-btn" title="Playback" @click="goPlayback(camera)"><span class="mi">history</span></button>
            <button class="act-btn" title="Settings" @click="openEditModal(camera)"><span class="mi">settings</span></button>
          </div>
        </div>
        <button class="dev-card add-card" @click="openCreateModal">
          <span class="mi">add_circle_outline</span>
          <span>Add New Device</span>
        </button>
      </div>
    </template>

    <div v-if="showModal" class="device-modal-overlay" @click.self="closeModal"></div>
    <div v-if="showModal" class="device-modal" :class="{ open: showModal }">
      <div class="dm-head">
        <h3>{{ editingCameraId == null ? 'Add Device' : 'Device Settings' }}</h3>
        <button class="drawer-close" @click="closeModal"><span class="mi">close</span></button>
      </div>
      <div class="dm-body">
        <div class="form-field dm-full">
          <label>Camera Name</label>
          <input v-model="formName" class="set-input" placeholder="Camera name">
        </div>
        <div class="form-field">
          <label>Resolution</label>
          <select v-model="formResolution" class="set-select">
            <option value="720p">720p</option>
            <option value="1080p">1080p</option>
            <option value="1440p">1440p</option>
            <option value="4K">4K</option>
          </select>
        </div>
        <div class="form-field">
          <label>Status</label>
          <select v-model="formStatus" class="set-select" :disabled="editingCameraId == null">
            <option value="online">Online</option>
            <option value="recording">Recording</option>
            <option value="offline">Offline</option>
          </select>
        </div>
        <div class="form-field dm-full">
          <label>Push Token</label>
          <div class="token-field">
            <span class="token-text" :title="editingCamera?.stream_key || latestCreatedToken">
              {{ editingCamera?.stream_key || latestCreatedToken || 'Will be generated after creating camera' }}
            </span>
            <button
              class="tiny-copy-btn"
              :disabled="!(editingCamera?.stream_key || latestCreatedToken)"
              @click="copyToken(editingCamera?.stream_key || latestCreatedToken)"
            >
              <span class="mi">{{ copiedToken === (editingCamera?.stream_key || latestCreatedToken) ? 'check' : 'content_copy' }}</span>
            </button>
          </div>
        </div>
        <div class="form-field dm-full">
          <label>Push URL</label>
          <div class="token-field">
            <span class="token-text" :title="editingCamera ? getPushUrl(editingCamera) : latestCreatedPushUrl">
              {{ (editingCamera ? getPushUrl(editingCamera) : latestCreatedPushUrl) || 'Will be generated after creating camera' }}
            </span>
            <button
              class="tiny-copy-btn"
              :disabled="!((editingCamera ? getPushUrl(editingCamera) : latestCreatedPushUrl))"
              @click="copyToken((editingCamera ? getPushUrl(editingCamera) : latestCreatedPushUrl))"
            >
              <span class="mi">{{ copiedToken === (editingCamera ? getPushUrl(editingCamera) : latestCreatedPushUrl) ? 'check' : 'content_copy' }}</span>
            </button>
          </div>
        </div>
        <div class="form-field dm-full">
          <label>Pull URL (FLV)</label>
          <div class="token-field">
            <span class="token-text" :title="editingCamera ? getPullFlvUrl(editingCamera) : latestCreatedPullUrl">
              {{ (editingCamera ? getPullFlvUrl(editingCamera) : latestCreatedPullUrl) || 'Will be generated after creating camera' }}
            </span>
            <button
              class="tiny-copy-btn"
              :disabled="!((editingCamera ? getPullFlvUrl(editingCamera) : latestCreatedPullUrl))"
              @click="copyToken((editingCamera ? getPullFlvUrl(editingCamera) : latestCreatedPullUrl))"
            >
              <span class="mi">{{ copiedToken === (editingCamera ? getPullFlvUrl(editingCamera) : latestCreatedPullUrl) ? 'check' : 'content_copy' }}</span>
            </button>
          </div>
        </div>
        <div v-if="formError" class="form-error dm-full">{{ formError }}</div>
      </div>
      <div class="dm-foot">
        <button v-if="editingCameraId != null" class="set-btn danger" :disabled="submitting" @click="deleteCamera(editingCameraId)"><span class="mi">delete</span> Delete</button>
        <div class="right">
          <button class="set-btn" :disabled="submitting" @click="closeModal">Cancel</button>
          <button class="set-btn primary" :disabled="submitting" @click="saveModal"><span class="mi">save</span> Save</button>
        </div>
      </div>
    </div>
  </div>
</template>

<style scoped>
.devices-page {
  display: flex;
  flex-direction: column;
  gap: 16px;
}

.device-toolbar {
  display: flex;
  align-items: center;
  justify-content: space-between;
  flex-wrap: wrap;
  gap: 12px;
}

.dt-left,
.dt-right {
  display: flex;
  align-items: center;
  gap: 8px;
  flex-wrap: wrap;
}

.filter-chips {
  display: flex;
  gap: 6px;
  flex-wrap: wrap;
}

.filter-chip {
  display: inline-flex;
  align-items: center;
  gap: 5px;
  height: 32px;
  padding: 0 14px;
  border-radius: var(--r6);
  border: 1px solid var(--olv);
  font: 500 13px/18px 'Roboto', sans-serif;
  color: var(--on-sfv);
  background: transparent;
  cursor: pointer;
}

.filter-chip.active {
  background: var(--pri-c);
  color: var(--on-pri-c);
  border-color: var(--pri-c);
}

.filter-chip .chip-count {
  background: rgba(255,255,255,.12);
  padding: 1px 6px;
  border-radius: 10px;
  font-size: 11px;
}

.view-toggle {
  display: flex;
  border: 1px solid var(--olv);
  border-radius: var(--r2);
  overflow: hidden;
}

.vt-btn {
  width: 36px;
  height: 34px;
  border: none;
  background: transparent;
  color: var(--on-sfv);
  display: flex;
  align-items: center;
  justify-content: center;
  cursor: pointer;
}

.vt-btn.active {
  background: var(--pri-c);
  color: var(--on-pri-c);
}

.vt-btn .mi {
  font-size: 20px;
}

.mt-btn {
  height: 34px;
  padding: 0 14px;
  border-radius: var(--r6);
  border: 1px solid var(--olv);
  background: transparent;
  color: var(--on-sfv);
  font: 500 13px/18px 'Roboto', sans-serif;
  cursor: pointer;
  display: flex;
  align-items: center;
  gap: 6px;
}

.mt-btn:disabled {
  opacity: .45;
  pointer-events: none;
}

.mt-btn.primary {
  background: var(--pri);
  color: var(--on-pri);
  border-color: var(--pri);
}

.token-banner {
  background: var(--sc);
  border: 1px solid var(--olv);
  border-radius: var(--r3);
  padding: 12px 14px;
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 12px;
  flex-wrap: wrap;
}

.token-banner-left {
  display: flex;
  align-items: center;
  gap: 10px;
}

.token-banner-left .mi {
  width: 34px;
  height: 34px;
  border-radius: 10px;
  background: rgba(0, 161, 255, .12);
  color: var(--pri);
  display: inline-flex;
  align-items: center;
  justify-content: center;
}

.token-banner-left h4 {
  font: 500 14px/20px 'Roboto', sans-serif;
}

.token-banner-left p {
  font: 500 12px/16px 'Roboto Mono', monospace;
  color: var(--on-sfv);
  word-break: break-all;
}

.token-banner-right {
  display: flex;
  align-items: center;
  gap: 8px;
}

.device-stats-row {
  display: grid;
  grid-template-columns: repeat(4, 1fr);
  gap: 12px;
}

.alert-stat-mini {
  background: var(--sc);
  border-radius: var(--r3);
  padding: 16px;
  display: flex;
  align-items: center;
  gap: 14px;
}

.asm-icon {
  width: 40px;
  height: 40px;
  border-radius: var(--r2);
  display: flex;
  align-items: center;
  justify-content: center;
  flex-shrink: 0;
}

.asm-icon .mi {
  font-size: 20px;
}

.asm-info h4 {
  font: 500 20px/26px 'Roboto', sans-serif;
}

.asm-info p {
  font: 400 12px/16px 'Roboto', sans-serif;
  color: var(--on-sfv);
}

.panel-state {
  background: var(--sc);
  border: 1px solid var(--olv);
  border-radius: var(--r3);
  padding: 20px;
  font: 400 13px/18px 'Roboto', sans-serif;
  color: var(--on-sfv);
}

.panel-state.error {
  color: #fca5a5;
}

.device-table-wrap {
  background: var(--sc);
  border-radius: var(--r3);
  overflow: hidden;
}

.device-table .dt-head,
.device-table .dt-row {
  display: grid;
  grid-template-columns: 40px 1fr 110px 110px 190px 80px 90px 136px;
  align-items: center;
  padding: 12px 18px;
}

.device-table .dt-head {
  background: var(--sc2);
  font: 500 12px/16px 'Roboto', sans-serif;
  color: var(--on-sfv);
  text-transform: uppercase;
  letter-spacing: .5px;
}

.device-table .dt-row {
  border-bottom: 1px solid var(--olv);
}

.dt-head .sortable {
  display: inline-flex;
  align-items: center;
  gap: 3px;
}

.dt-head .sortable .mi {
  font-size: 14px;
}

.dt-cb input {
  width: 16px;
  height: 16px;
  accent-color: var(--pri);
}

.dt-device {
  display: flex;
  align-items: center;
  gap: 12px;
  min-width: 0;
}

.dev-thumb {
  width: 48px;
  height: 32px;
  border-radius: var(--r1);
  background: linear-gradient(135deg, #0d0d15, #1c1728);
  display: flex;
  align-items: center;
  justify-content: center;
  overflow: hidden;
}

.dev-thumb img {
  width: 100%;
  height: 100%;
  object-fit: cover;
}

.dev-thumb .mi {
  font-size: 16px;
  color: rgba(255,255,255,.2);
}

.dev-info {
  min-width: 0;
}

.dev-info h4 {
  font: 500 14px/20px 'Roboto', sans-serif;
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}

.dev-info p {
  font: 400 12px/16px 'Roboto', sans-serif;
  color: var(--on-sfv);
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}

.dt-status {
  display: flex;
  align-items: center;
  gap: 6px;
  font: 500 13px/18px 'Roboto', sans-serif;
}

.dt-status .dot {
  width: 7px;
  height: 7px;
  border-radius: 50%;
}

.dt-status .dot.on { background: var(--green); }
.dt-status .dot.off { background: var(--red); }
.dt-status .dot.rec { background: var(--orange); }

.dt-status .hb-chip,
.dc-hb-badge {
  padding: 1px 6px;
  border-radius: var(--r1);
  font: 600 10px/14px 'Roboto', sans-serif;
  letter-spacing: .3px;
  border: 1px solid transparent;
}

.dt-status .hb-chip.hb-good,
.dc-hb-badge.hb-good {
  background: rgba(50, 142, 94, .16);
  color: #98e2b0;
  border-color: rgba(50, 142, 94, .35);
}

.dt-status .hb-chip.hb-weak,
.dc-hb-badge.hb-weak {
  background: rgba(255, 158, 67, .16);
  color: #ffc38e;
  border-color: rgba(255, 158, 67, .35);
}

.dt-status .hb-chip.hb-stale,
.dc-hb-badge.hb-stale {
  background: rgba(255, 107, 107, .16);
  color: #ffb3b3;
  border-color: rgba(255, 107, 107, .35);
}

.dt-cell {
  font: 400 13px/18px 'Roboto', sans-serif;
  color: var(--on-sfv);
}

.token-cell {
  display: flex;
  align-items: center;
  gap: 6px;
}

.token-text {
  font: 500 12px/16px 'Roboto Mono', monospace;
  color: var(--on-sf);
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}

.tiny-copy-btn {
  width: 24px;
  height: 24px;
  border-radius: 6px;
  border: 1px solid var(--olv);
  background: transparent;
  color: var(--on-sfv);
  display: inline-flex;
  align-items: center;
  justify-content: center;
  cursor: pointer;
  flex-shrink: 0;
}

.tiny-copy-btn:disabled {
  opacity: .4;
  cursor: not-allowed;
}

.tiny-copy-btn .mi {
  font-size: 14px;
}

.dt-actions {
  display: flex;
  gap: 4px;
  justify-content: flex-end;
}

.dt-actions .act-btn,
.dc-footer .act-btn {
  width: 28px;
  height: 28px;
  border-radius: 50%;
  border: none;
  background: transparent;
  display: flex;
  align-items: center;
  justify-content: center;
  cursor: pointer;
}

.dt-actions .act-btn .mi,
.dc-footer .act-btn .mi {
  font-size: 18px;
  color: var(--on-sfv);
}

.device-card-grid {
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(280px, 1fr));
  gap: 14px;
}

.dev-card {
  background: var(--sc);
  border-radius: var(--r3);
  overflow: hidden;
}

.dc-thumb {
  width: 100%;
  aspect-ratio: 16/9;
  background: linear-gradient(135deg, #0d0d15, #1c1728);
  position: relative;
  overflow: hidden;
  display: flex;
  align-items: center;
  justify-content: center;
}

.dc-thumb img {
  width: 100%;
  height: 100%;
  object-fit: cover;
}

.dc-thumb .mi {
  font-size: 36px;
  color: rgba(255,255,255,.1);
}

.dc-thumb .sv-grid {
  position: absolute;
  inset: 0;
  background: linear-gradient(rgba(255,255,255,.015) 1px, transparent 1px), linear-gradient(90deg, rgba(255,255,255,.015) 1px, transparent 1px);
  background-size: 20px 20px;
}

.dc-status-badge {
  position: absolute;
  top: 8px;
  left: 8px;
  display: flex;
  align-items: center;
  gap: 4px;
  padding: 3px 8px;
  border-radius: var(--r1);
  font: 500 10px/16px 'Roboto', sans-serif;
  color: #fff;
}

.dc-status-badge.online { background: rgba(60,160,70,.85); }
.dc-status-badge.offline { background: rgba(160,60,60,.9); }
.dc-status-badge.recording { background: rgba(200,60,60,.9); }

.dc-status-badge .dot {
  width: 5px;
  height: 5px;
  border-radius: 50%;
  background: #fff;
}

.dc-hb-badge {
  position: absolute;
  top: 8px;
  right: 8px;
}

.dc-body {
  padding: 14px;
}

.dc-body h4 {
  font: 500 14px/20px 'Roboto', sans-serif;
  margin-bottom: 4px;
}

.dc-meta {
  display: flex;
  flex-direction: column;
  gap: 4px;
}

.dc-meta-row {
  display: flex;
  align-items: center;
  gap: 6px;
  font: 400 12px/16px 'Roboto', sans-serif;
  color: var(--on-sfv);
}

.dc-meta-row .mi {
  font-size: 14px;
  color: var(--ol);
}

.token-row {
  justify-content: space-between;
}

.dc-footer {
  display: flex;
  justify-content: flex-end;
  gap: 4px;
  padding: 0 14px 12px;
}

.dev-card.add-card {
  border: 2px dashed var(--olv);
  background: transparent;
  min-height: 220px;
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  gap: 10px;
}

.dev-card.add-card .mi {
  font-size: 36px;
  color: var(--olv);
}

.dev-card.add-card span {
  font: 500 14px/20px 'Roboto', sans-serif;
  color: var(--ol);
}

.device-modal-overlay {
  position: fixed;
  inset: 0;
  background: rgba(0,0,0,.55);
  z-index: 550;
}

.device-modal {
  position: fixed;
  top: 50%;
  left: 50%;
  width: min(560px, calc(100% - 24px));
  transform: translate(-50%, -50%);
  z-index: 551;
  background: var(--sc);
  border-radius: var(--r3);
  display: flex;
  flex-direction: column;
  box-shadow: var(--e3);
}

.dm-head {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 16px 18px;
  border-bottom: 1px solid var(--olv);
}

.dm-head h3 {
  font: 500 16px/24px 'Roboto', sans-serif;
}

.drawer-close {
  width: 30px;
  height: 30px;
  border-radius: 50%;
  border: 1px solid var(--olv);
  background: transparent;
  color: var(--on-sfv);
}

.dm-body {
  padding: 16px 18px;
  display: grid;
  grid-template-columns: 1fr 1fr;
  gap: 12px;
}

.dm-foot {
  display: flex;
  justify-content: space-between;
  gap: 8px;
  padding: 14px 18px;
  border-top: 1px solid var(--olv);
}

.dm-foot .right {
  display: flex;
  gap: 8px;
}

.dm-full {
  grid-column: 1 / -1;
}

.form-field {
  display: grid;
  gap: 6px;
}

.form-field label {
  font: 500 12px/16px 'Roboto', sans-serif;
  color: var(--on-sfv);
}

.set-input,
.set-select {
  width: 100%;
  height: 36px;
  border-radius: var(--r2);
  border: 1px solid var(--olv);
  background: var(--sc2);
  color: var(--on-sf);
  padding: 0 10px;
  font: 400 13px/18px 'Roboto', sans-serif;
}

.token-field {
  min-height: 36px;
  border-radius: var(--r2);
  border: 1px solid var(--olv);
  background: var(--sc2);
  color: var(--on-sf);
  padding: 6px 10px;
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 8px;
}

.set-btn {
  height: 34px;
  border-radius: var(--r6);
  border: 1px solid var(--olv);
  background: transparent;
  color: var(--on-sfv);
  padding: 0 12px;
  font: 500 13px/18px 'Roboto', sans-serif;
  display: inline-flex;
  align-items: center;
  gap: 6px;
}

.set-btn.primary {
  background: var(--pri);
  color: var(--on-pri);
  border-color: var(--pri);
}

.set-btn.danger {
  color: var(--red);
  border-color: rgba(255,107,107,.4);
}

.form-error {
  color: #fca5a5;
  font: 400 12px/16px 'Roboto', sans-serif;
}

@media (max-width: 1439px) {
  .device-stats-row {
    grid-template-columns: repeat(2, 1fr);
  }

  .device-table .dt-head,
  .device-table .dt-row {
    grid-template-columns: 40px 1fr 100px 100px 160px 80px 120px;
  }

  .device-table .dt-head > :nth-child(7),
  .device-table .dt-row > :nth-child(7) {
    display: none;
  }
}

@media (max-width: 1023px) {
  .device-stats-row {
    grid-template-columns: 1fr;
  }

  .device-table .dt-head,
  .device-table .dt-row {
    grid-template-columns: 40px 1fr 90px 100px 110px;
  }

  .device-table .dt-head > :nth-child(6),
  .device-table .dt-row > :nth-child(6),
  .device-table .dt-head > :nth-child(7),
  .device-table .dt-row > :nth-child(7),
  .device-table .dt-head > :nth-child(8),
  .device-table .dt-row > :nth-child(8) {
    display: none;
  }

  .dm-body {
    grid-template-columns: 1fr;
  }
}
</style>
