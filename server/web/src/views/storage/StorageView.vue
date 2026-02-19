<script setup>
import { computed, onBeforeUnmount, onMounted, ref } from 'vue'
import { storageApi } from '../../api/index.js'

const loading = ref(false)
const error = ref('')

const overview = ref(null)
const cloudConfig = ref(null)
const trend = ref([])
const devices = ref([])
const trendScope = ref('all')
const trendCameraId = ref(null)
let refreshTimer = null

const savingPolicy = ref(false)
const savingCloud = ref(false)
const policyMessage = ref('')
const policy = ref({
  min_free_percent: 15,
  apply_all: true,
  camera_id: null,
})

const cloudOverview = computed(() => overview.value?.cloud || {})
const usedPercent = computed(() => clamp(Number(cloudOverview.value?.usedPercent || 0), 0, 100))
const total = computed(() => Number(cloudOverview.value?.totalGb || 0))
const used = computed(() => Number(cloudOverview.value?.usedGb || 0))
const free = computed(() => Math.max(0, total.value - used.value))
const cloudStatusLabel = computed(() => {
  const status = String(cloudOverview.value?.syncStatus || 'disconnected').toLowerCase()
  if (status === 'connected') return 'Connected'
  if (status === 'syncing') return 'Syncing'
  return 'Disconnected'
})
const cloudStatusClass = computed(() => {
  const status = String(cloudOverview.value?.syncStatus || 'disconnected').toLowerCase()
  if (status === 'connected') return 'cs-connected'
  if (status === 'syncing') return 'cs-syncing'
  return 'cs-disconnected'
})

const ringDash = 264
const ringOffset = computed(() => ringDash * (1 - usedPercent.value / 100))

const breakdownItems = computed(() => {
  const b = overview.value?.breakdown || {}
  const video = Number(b.video || used.value || 0)
  const snapshots = Number(b.snapshots || 0)
  const cache = Number(b.cache || 0)
  const safeTotal = Math.max(0.1, total.value)
  return [
    { key: 'video', label: 'Video Recordings', value: video, pct: (video / safeTotal) * 100, color: 'var(--pri)' },
    { key: 'snapshots', label: 'Snapshots', value: snapshots, pct: (snapshots / safeTotal) * 100, color: 'var(--ter)' },
    { key: 'cache', label: 'Cache', value: cache, pct: (cache / safeTotal) * 100, color: 'var(--orange)' },
    { key: 'free', label: 'Available', value: free.value, pct: (free.value / safeTotal) * 100, color: 'var(--red)' },
  ]
})

const onlineDevices = computed(() => devices.value.filter((item) => item.status !== 'offline'))
const selectedCameraId = computed(() => Number(policy.value.camera_id || 0))
const selectedCamera = computed(() => {
  if (!selectedCameraId.value) return null
  return devices.value.find((item) => Number(item.cameraId) === selectedCameraId.value) || null
})
const canSavePolicy = computed(() => {
  if (savingPolicy.value) return false
  if (policy.value.apply_all) return true
  return !!selectedCamera.value
})

const predictedDaysToFull = computed(() => {
  if (trend.value.length < 2) return null
  const first = Number(trend.value[0]?.usage || 0)
  const last = Number(trend.value[trend.value.length - 1]?.usage || 0)
  const growth = (last - first) / Math.max(1, trend.value.length - 1)
  if (growth <= 0) return null
  const remain = Math.max(0, 100 - last)
  return Math.max(1, Math.round(remain / growth))
})

const trendLabels = computed(() => {
  if (!trend.value.length) return []
  const first = trend.value[0]
  const mid = trend.value[Math.floor((trend.value.length - 1) / 2)]
  const last = trend.value[trend.value.length - 1]
  return [first, mid, last]
})
const trendCameraOptions = computed(() => {
  return devices.value.filter((item) => item.status !== 'offline')
})

onMounted(() => {
  void refreshData()
  refreshTimer = setInterval(() => {
    refreshDevicesOnly().catch(() => {})
  }, 10000)
})

onBeforeUnmount(() => {
  if (refreshTimer) {
    clearInterval(refreshTimer)
    refreshTimer = null
  }
})

async function refreshData() {
  loading.value = true
  error.value = ''
  policyMessage.value = ''
  try {
    const [ov, byDev] = await Promise.all([
      storageApi.getOverview(),
      storageApi.getByDevice(),
    ])
    overview.value = ov
    devices.value = Array.isArray(byDev?.devices) ? byDev.devices : []
    cloudConfig.value = {
      enabled: Boolean(ov?.cloud?.enabled),
      provider: String(ov?.cloud?.provider || 's3'),
      bucket: String(ov?.cloud?.bucket || ''),
      region: String(ov?.cloud?.region || ''),
      endpoint: String(ov?.cloud?.endpoint || ''),
      totalGb: Number(ov?.cloud?.totalGb || 2000),
      usedGb: Number(ov?.cloud?.usedGb || 0),
    }
    const policyFromServer = ov?.policy || {}
    policy.value.min_free_percent = clamp(Number(policyFromServer.min_free_percent || policy.value.min_free_percent), 1, 95)
    await refreshTrend()
  } catch (err) {
    error.value = err?.message || 'Failed to load storage data'
  } finally {
    loading.value = false
  }
}

async function refreshDevicesOnly() {
  try {
    const byDev = await storageApi.getByDevice()
    devices.value = Array.isArray(byDev?.devices) ? byDev.devices : []
  } catch {
  }
}

async function saveCloudConfig() {
  if (!cloudConfig.value) return
  savingCloud.value = true
  policyMessage.value = ''
  try {
    await storageApi.updateCloudConfig({
      enabled: !!cloudConfig.value.enabled,
      provider: String(cloudConfig.value.provider || 's3'),
      bucket: String(cloudConfig.value.bucket || '').trim(),
      region: String(cloudConfig.value.region || '').trim(),
      endpoint: String(cloudConfig.value.endpoint || '').trim(),
      total_gb: Math.max(1, Number(cloudConfig.value.totalGb || 2000)),
      used_gb: Math.max(0, Number(cloudConfig.value.usedGb || 0)),
    })
    policyMessage.value = 'Cloud storage config saved'
    await refreshData()
  } catch (err) {
    policyMessage.value = err?.message || 'Cloud config save failed'
  } finally {
    savingCloud.value = false
  }
}

async function refreshTrend() {
  const cameraId = trendScope.value === 'single' ? Number(trendCameraId.value || 0) : null
  const tr = await storageApi.getTrend(14, cameraId > 0 ? cameraId : null)
  trend.value = Array.isArray(tr?.trend) ? tr.trend : []
}

async function savePolicy() {
  savingPolicy.value = true
  policyMessage.value = ''
  try {
    const payload = {
      min_free_percent: clamp(Number(policy.value.min_free_percent || 15), 1, 95),
      apply_all: !!policy.value.apply_all,
    }
    if (!payload.apply_all && selectedCamera.value) {
      payload.camera_id = selectedCamera.value.cameraId
    }
    const data = await storageApi.updatePolicy(payload)
    policy.value.min_free_percent = Number(data?.policy?.min_free_percent || payload.min_free_percent)
    policyMessage.value = 'Policy pushed to device(s)'
    await refreshData()
  } catch (err) {
    policyMessage.value = err?.message || 'Save failed'
  } finally {
    savingPolicy.value = false
  }
}

function clearDevice(device) {
  policy.value.apply_all = false
  policy.value.camera_id = device.cameraId
  policyMessage.value = `Selected ${device.name}`
}

function onTrendScopeChange() {
  if (trendScope.value === 'single' && !trendCameraId.value) {
    trendCameraId.value = trendCameraOptions.value[0]?.cameraId || null
  }
  refreshTrend().catch(() => {})
}

function clamp(value, min, max) {
  return Math.max(min, Math.min(max, value))
}

function formatGB(value) {
  return `${Number(value || 0).toFixed(1)} GB`
}

function formatDateLabel(dateText) {
  if (!dateText) return '--'
  const d = new Date(`${dateText}T00:00:00`)
  if (Number.isNaN(d.getTime())) return dateText
  return d.toLocaleDateString([], { month: 'short', day: 'numeric' })
}

function formatCloudSyncTime(ts) {
  const t = Number(ts || 0)
  if (!Number.isFinite(t) || t <= 0) return 'Never'
  return new Date(t).toLocaleString()
}

function formatLastUpdate(ts) {
  const t = Number(ts || 0)
  if (!Number.isFinite(t) || t <= 0) return 'No telemetry'
  const diffSec = Math.max(0, Math.floor((Date.now() - t) / 1000))
  if (diffSec < 5) return 'Updated just now'
  if (diffSec < 60) return `Updated ${diffSec}s ago`
  const mins = Math.floor(diffSec / 60)
  if (mins < 60) return `Updated ${mins}m ago`
  const hours = Math.floor(mins / 60)
  if (hours < 24) return `Updated ${hours}h ago`
  const days = Math.floor(hours / 24)
  return `Updated ${days}d ago`
}

function statusLabel(status) {
  const s = String(status || 'offline').toLowerCase()
  if (s === 'streaming') return 'Streaming'
  if (s === 'online') return 'Online'
  return 'Offline'
}

function statusClass(status) {
  const s = String(status || 'offline').toLowerCase()
  if (s === 'streaming') return 'st-streaming'
  if (s === 'online') return 'st-online'
  return 'st-offline'
}
</script>

<template>
  <div class="storage-page">
    <div v-if="loading" class="panel-state">Loading storage data...</div>
    <div v-else-if="error" class="panel-state error">{{ error }}</div>

    <template v-else>
      <div class="storage-grid">
        <div class="storage-overview">
          <div class="storage-ring">
            <svg viewBox="0 0 100 100">
              <circle class="ring-bg" cx="50" cy="50" r="42"></circle>
              <circle class="ring-fill ring-used" cx="50" cy="50" r="42" :stroke-dasharray="ringDash" :stroke-dashoffset="ringOffset"></circle>
            </svg>
            <div class="ring-center">
              <span class="ring-pct">{{ usedPercent }}%</span>
              <span class="ring-label">Used</span>
            </div>
          </div>
          <div class="storage-detail">
            <h3>Cloud Storage Overview</h3>
            <p class="sd-sub">{{ formatGB(used) }} of {{ formatGB(total) }} used</p>
            <div class="cloud-status-row">
              <span class="cloud-status-pill" :class="cloudStatusClass">{{ cloudStatusLabel }}</span>
              <span class="cloud-meta">Provider {{ cloudOverview.provider || '-' }}</span>
              <span class="cloud-meta">Bucket {{ cloudOverview.bucket || '-' }}</span>
              <span class="cloud-meta">Last Sync {{ formatCloudSyncTime(cloudOverview.lastSyncMs) }}</span>
            </div>
            <div class="storage-breakdown">
              <div v-for="item in breakdownItems" :key="item.key" class="sb-item">
                <span class="sb-dot" :style="{ background: item.color }"></span>
                <div class="sb-info">
                  <div class="sb-name"><span>{{ item.label }}</span><span>{{ formatGB(item.value) }}</span></div>
                  <div class="sb-bar"><div class="sb-fill" :style="{ width: `${clamp(item.pct, 0, 100)}%`, background: item.color }"></div></div>
                </div>
              </div>
            </div>
          </div>
        </div>

        <div class="storage-trend">
          <h3>Edge Cache Trend</h3>
          <div class="trend-toolbar">
            <p class="st-sub">Device-side cache usage in last 14 days</p>
            <div class="trend-filters">
              <select v-model="trendScope" @change="onTrendScopeChange">
                <option value="all">All devices</option>
                <option value="single">Single device</option>
              </select>
              <select v-model.number="trendCameraId" :disabled="trendScope !== 'single'" @change="refreshTrend">
                <option :value="null">Select camera</option>
                <option v-for="device in trendCameraOptions" :key="device.cameraId" :value="device.cameraId">{{ device.name }}</option>
              </select>
            </div>
          </div>
          <div class="trend-chart">
            <div
              v-for="item in trend"
              :key="item.date"
              class="tc-bar"
              :style="{ height: `${clamp(Number(item.usage || 0), 2, 100)}%` }"
            ></div>
          </div>
          <div class="trend-labels">
            <span v-for="item in trendLabels" :key="item.date">{{ formatDateLabel(item.date) }}</span>
          </div>
          <div class="trend-predict">
            <span class="mi">info</span>
            <span>
              <template v-if="predictedDaysToFull">Estimated full in <strong>{{ predictedDaysToFull }} days</strong> at current rate</template>
              <template v-else>No growth trend detected</template>
            </span>
          </div>
        </div>
      </div>

      <div v-if="cloudConfig" class="storage-policy">
        <h3>Cloud Storage Settings</h3>
        <div class="policy-grid">
          <div class="policy-card">
            <div class="pc-top">
              <h4><span class="mi">cloud_done</span> Enable Cloud Sync</h4>
            </div>
            <p class="pc-desc">Enable/disable cloud backup channel</p>
            <div class="pc-value">
              <select v-model="cloudConfig.enabled">
                <option :value="true">Enabled</option>
                <option :value="false">Disabled</option>
              </select>
            </div>
          </div>

          <div class="policy-card">
            <div class="pc-top">
              <h4><span class="mi">dns</span> Cloud Provider</h4>
            </div>
            <p class="pc-desc">Cloud backend type</p>
            <div class="pc-value">
              <select v-model="cloudConfig.provider">
                <option value="s3">S3 Compatible</option>
                <option value="oss">Alibaba OSS</option>
                <option value="cos">Tencent COS</option>
              </select>
            </div>
          </div>

          <div class="policy-card">
            <div class="pc-top">
              <h4><span class="mi">inventory_2</span> Bucket</h4>
            </div>
            <p class="pc-desc">Cloud bucket/container name</p>
            <div class="pc-value">
              <input v-model="cloudConfig.bucket" class="cloud-input" placeholder="reallive-recordings" />
            </div>
          </div>

          <div class="policy-card">
            <div class="pc-top">
              <h4><span class="mi">public</span> Region / Endpoint</h4>
            </div>
            <p class="pc-desc">Region and endpoint</p>
            <div class="pc-value cloud-double">
              <input v-model="cloudConfig.region" class="cloud-input" placeholder="ap-southeast-1" />
              <input v-model="cloudConfig.endpoint" class="cloud-input" placeholder="s3.amazonaws.com" />
            </div>
          </div>
        </div>
        <div class="policy-actions">
          <span class="policy-msg">{{ policyMessage }}</span>
          <button class="save-btn" :disabled="savingCloud || !cloudConfig" @click="saveCloudConfig">{{ savingCloud ? 'Saving...' : 'Save Cloud Config' }}</button>
        </div>
      </div>

      <div class="storage-device-table">
        <h3>Storage by Device (Edge Cache & Status)</h3>
        <div class="sdt-row header">
          <div>Device</div>
          <div>Status</div>
          <div>Total</div>
          <div>Free</div>
          <div>Usage</div>
          <div></div>
        </div>
        <div v-if="!devices.length" class="panel-state">No device storage data.</div>
        <div v-for="device in devices" :key="device.id" class="sdt-row">
          <div class="sdt-device">
            <span class="mi">videocam</span>
            <span>{{ device.name }}</span>
          </div>
          <div class="sdt-status">
            <span :class="['status-dot', statusClass(device.status)]"></span>
            <span>{{ statusLabel(device.status) }}</span>
          </div>
          <div class="sdt-cell">{{ formatGB(device.totalGb) }}</div>
          <div class="sdt-cell">{{ formatGB(device.freeGb) }}</div>
          <div class="sdt-bar">
            <div class="bar-track"><div class="bar-fill" :style="{ width: `${clamp(Number(device.usedPercent || 0), 0, 100)}%`, background: 'var(--pri)' }"></div></div>
            <span class="bar-pct">{{ clamp(Number(device.usedPercent || 0), 0, 100) }}%</span>
          </div>
          <div class="sdt-actions">
            <button @click="clearDevice(device)"><span class="mi">tune</span> Apply Policy</button>
            <div class="dev-policy">Min free {{ device.minFreePercent }}%</div>
            <div class="dev-last">{{ formatLastUpdate(device.lastUpdateMs) }}</div>
          </div>
        </div>
      </div>

      <div class="storage-policy">
        <h3>Edge Cache Cleanup Policy</h3>
        <div class="policy-grid">
          <div class="policy-card">
            <div class="pc-top">
              <h4><span class="mi">delete_sweep</span> Start Cleanup Threshold</h4>
            </div>
            <p class="pc-desc">Start deleting oldest recordings when free storage drops below this value</p>
            <div class="pc-value">
              <select v-model.number="policy.min_free_percent">
                <option v-for="pct in [5,10,12,15,18,20,25,30]" :key="pct" :value="pct">{{ pct }}%</option>
              </select>
            </div>
          </div>

          <div class="policy-card">
            <div class="pc-top">
              <h4><span class="mi">devices</span> Apply Scope</h4>
            </div>
            <p class="pc-desc">Choose all cameras or one specific device</p>
            <div class="pc-value">
              <select v-model="policy.apply_all">
                <option :value="true">All cameras</option>
                <option :value="false">Single camera</option>
              </select>
            </div>
          </div>

          <div class="policy-card">
            <div class="pc-top">
              <h4><span class="mi">videocam</span> Target Camera</h4>
            </div>
            <p class="pc-desc">Used only when apply scope is single camera</p>
            <div class="pc-value">
              <select v-model.number="policy.camera_id" :disabled="policy.apply_all">
                <option :value="null">Select camera</option>
                <option v-for="device in onlineDevices" :key="device.cameraId" :value="device.cameraId">{{ device.name }}</option>
              </select>
            </div>
          </div>
        </div>

        <div class="policy-actions">
          <span class="policy-msg">{{ policyMessage }}</span>
          <button class="save-btn" :disabled="!canSavePolicy" @click="savePolicy">{{ savingPolicy ? 'Saving...' : 'Save Policy' }}</button>
        </div>
      </div>
    </template>
  </div>
</template>

<style scoped>
.storage-page {
  display: flex;
  flex-direction: column;
  gap: 24px;
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

.storage-grid {
  display: grid;
  grid-template-columns: 1fr 1fr;
  gap: 16px;
}

.storage-overview {
  background: var(--sc);
  border-radius: var(--r3);
  padding: 24px;
  display: flex;
  align-items: center;
  gap: 32px;
}

.storage-ring {
  position: relative;
  width: 160px;
  height: 160px;
  flex-shrink: 0;
}

.storage-ring svg {
  width: 100%;
  height: 100%;
  transform: rotate(-90deg);
}

.storage-ring .ring-bg {
  fill: none;
  stroke: var(--sc2);
  stroke-width: 14;
}

.storage-ring .ring-fill {
  fill: none;
  stroke-width: 14;
  stroke-linecap: round;
  stroke: var(--pri);
  transition: stroke-dashoffset .8s ease;
}

.ring-center {
  position: absolute;
  inset: 0;
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
}

.ring-pct {
  font: 500 32px/40px 'Roboto', sans-serif;
}

.ring-label {
  font: 400 12px/16px 'Roboto', sans-serif;
  color: var(--on-sfv);
}

.storage-detail {
  flex: 1;
}

.storage-detail h3 {
  font: 500 16px/24px 'Roboto', sans-serif;
  margin-bottom: 4px;
}

.sd-sub {
  font: 400 13px/18px 'Roboto', sans-serif;
  color: var(--on-sfv);
  margin-bottom: 16px;
}

.cloud-status-row {
  display: flex;
  flex-wrap: wrap;
  align-items: center;
  gap: 8px;
  margin-bottom: 14px;
}

.cloud-status-pill {
  padding: 2px 8px;
  border-radius: var(--r1);
  font: 600 11px/16px 'Roboto', sans-serif;
  border: 1px solid transparent;
}

.cloud-status-pill.cs-connected {
  color: #98e2b0;
  background: rgba(50, 142, 94, 0.16);
  border-color: rgba(50, 142, 94, 0.35);
}

.cloud-status-pill.cs-syncing {
  color: #ffc38e;
  background: rgba(255, 158, 67, 0.16);
  border-color: rgba(255, 158, 67, 0.35);
}

.cloud-status-pill.cs-disconnected {
  color: #ffb3b3;
  background: rgba(255, 107, 107, 0.16);
  border-color: rgba(255, 107, 107, 0.35);
}

.cloud-meta {
  font: 400 11px/16px 'Roboto', sans-serif;
  color: var(--on-sfv);
}

.storage-breakdown {
  display: flex;
  flex-direction: column;
  gap: 12px;
}

.sb-item {
  display: flex;
  align-items: center;
  gap: 12px;
}

.sb-item .sb-dot {
  width: 10px;
  height: 10px;
  border-radius: 3px;
  flex-shrink: 0;
}

.sb-item .sb-info {
  flex: 1;
  min-width: 0;
}

.sb-item .sb-info .sb-name {
  font: 400 13px/18px 'Roboto', sans-serif;
  display: flex;
  justify-content: space-between;
  margin-bottom: 4px;
}

.sb-item .sb-info .sb-name span:last-child {
  color: var(--on-sfv);
}

.sb-item .sb-bar {
  width: 100%;
  height: 4px;
  border-radius: 2px;
  background: var(--sc2);
  overflow: hidden;
}

.sb-item .sb-bar .sb-fill {
  height: 100%;
  border-radius: 2px;
}

.storage-trend {
  background: var(--sc);
  border-radius: var(--r3);
  padding: 24px;
}

.storage-trend h3 {
  font: 500 16px/24px 'Roboto', sans-serif;
  margin-bottom: 4px;
}

.st-sub {
  font: 400 13px/18px 'Roboto', sans-serif;
  color: var(--on-sfv);
}

.trend-toolbar {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 12px;
  margin-bottom: 16px;
}

.trend-filters {
  display: flex;
  align-items: center;
  gap: 8px;
}

.trend-filters select {
  height: 30px;
  padding: 0 8px;
  border-radius: var(--r2);
  border: 1px solid var(--olv);
  background: var(--sc2);
  color: var(--on-sf);
  font: 400 12px/16px 'Roboto', sans-serif;
}

.trend-chart {
  height: 140px;
  display: flex;
  align-items: flex-end;
  gap: 3px;
  padding-bottom: 24px;
  border-bottom: 1px solid var(--olv);
}

.trend-chart .tc-bar {
  flex: 1;
  border-radius: var(--r1) var(--r1) 0 0;
  min-height: 4px;
  background: var(--pri);
  opacity: .75;
}

.trend-labels {
  display: flex;
  justify-content: space-between;
  margin-top: 6px;
}

.trend-labels span {
  font: 400 10px/14px 'Roboto', sans-serif;
  color: var(--ol);
}

.trend-predict {
  display: flex;
  align-items: center;
  gap: 8px;
  margin-top: 16px;
  padding: 10px 14px;
  border-radius: var(--r2);
  background: rgba(255,158,67,.08);
  border: 1px solid rgba(255,158,67,.2);
}

.trend-predict .mi {
  font-size: 18px;
  color: var(--orange);
}

.trend-predict span {
  font: 400 13px/18px 'Roboto', sans-serif;
  color: var(--on-sfv);
}

.trend-predict strong {
  color: var(--orange);
}

.storage-device-table {
  background: var(--sc);
  border-radius: var(--r3);
  overflow: hidden;
}

.storage-device-table h3 {
  font: 500 15px/22px 'Roboto', sans-serif;
  padding: 16px 18px;
  border-bottom: 1px solid var(--olv);
}

.sdt-row {
  display: grid;
  grid-template-columns: 1fr 120px 110px 110px 1fr 140px;
  align-items: center;
  padding: 12px 18px;
  border-bottom: 1px solid var(--olv);
  gap: 12px;
}

.sdt-row.header {
  font: 500 12px/16px 'Roboto', sans-serif;
  color: var(--on-sfv);
  text-transform: uppercase;
  letter-spacing: .5px;
  background: var(--sc2);
}

.sdt-row .sdt-device {
  display: flex;
  align-items: center;
  gap: 10px;
  min-width: 0;
}

.sdt-row .sdt-device .mi {
  font-size: 18px;
  color: var(--on-sfv);
}

.sdt-row .sdt-device span {
  font: 400 14px/20px 'Roboto', sans-serif;
}

.sdt-row .sdt-cell {
  font: 400 13px/18px 'Roboto', sans-serif;
  color: var(--on-sfv);
}

.sdt-row .sdt-status {
  display: flex;
  align-items: center;
  gap: 6px;
  font: 500 12px/16px 'Roboto', sans-serif;
  color: var(--on-sfv);
}

.sdt-row .sdt-status .status-dot {
  width: 8px;
  height: 8px;
  border-radius: 50%;
}

.sdt-row .sdt-status .status-dot.st-streaming { background: var(--orange); }
.sdt-row .sdt-status .status-dot.st-online { background: var(--green); }
.sdt-row .sdt-status .status-dot.st-offline { background: var(--red); }

.sdt-row .sdt-bar {
  display: flex;
  align-items: center;
  gap: 8px;
}

.sdt-row .sdt-bar .bar-track {
  flex: 1;
  height: 6px;
  border-radius: 3px;
  background: var(--sc2);
  overflow: hidden;
}

.sdt-row .sdt-bar .bar-fill {
  height: 100%;
  border-radius: 3px;
}

.sdt-row .sdt-bar .bar-pct {
  font: 500 12px/16px 'Roboto', sans-serif;
  min-width: 36px;
  text-align: right;
}

.sdt-row .sdt-actions button {
  height: 28px;
  padding: 0 10px;
  border-radius: var(--r6);
  border: 1px solid var(--olv);
  background: transparent;
  color: var(--on-sfv);
  font: 400 12px/16px 'Roboto', sans-serif;
  display: inline-flex;
  align-items: center;
  gap: 4px;
}

.sdt-row .sdt-actions {
  display: flex;
  flex-direction: column;
  gap: 6px;
  align-items: flex-start;
}

.sdt-row .sdt-actions button .mi {
  font-size: 14px;
}

.sdt-row .sdt-actions .dev-policy {
  font: 400 11px/16px 'Roboto', sans-serif;
  color: var(--on-sfv);
}

.sdt-row .sdt-actions .dev-last {
  font: 400 11px/16px 'Roboto', sans-serif;
  color: var(--ol);
}

.storage-policy {
  background: var(--sc);
  border-radius: var(--r3);
  padding: 20px;
}

.storage-policy h3 {
  font: 500 15px/22px 'Roboto', sans-serif;
  margin-bottom: 16px;
}

.policy-grid {
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(260px, 1fr));
  gap: 14px;
}

.policy-card {
  background: var(--sc2);
  border-radius: var(--r2);
  padding: 16px;
  display: flex;
  flex-direction: column;
  gap: 10px;
}

.policy-card .pc-top {
  display: flex;
  align-items: center;
  justify-content: space-between;
}

.policy-card .pc-top h4 {
  font: 500 14px/20px 'Roboto', sans-serif;
  display: flex;
  align-items: center;
  gap: 8px;
}

.policy-card .pc-top h4 .mi {
  font-size: 20px;
  color: var(--pri);
}

.sw-web {
  width: 42px;
  height: 24px;
  border-radius: 12px;
  background: var(--sfv);
  position: relative;
  border: none;
}

.sw-web.on {
  background: var(--pri);
}

.sw-web::after {
  content: '';
  position: absolute;
  top: 3px;
  left: 3px;
  width: 18px;
  height: 18px;
  border-radius: 50%;
  background: var(--ol);
}

.sw-web.on::after {
  left: 21px;
  background: var(--on-pri);
}

.policy-card .pc-desc {
  font: 400 12px/18px 'Roboto', sans-serif;
  color: var(--on-sfv);
}

.policy-card .pc-value select {
  height: 32px;
  padding: 0 8px;
  border-radius: var(--r2);
  border: 1px solid var(--olv);
  background: var(--sc);
  color: var(--on-sf);
  font: 400 13px/18px 'Roboto', sans-serif;
  width: 100%;
}

.cloud-input {
  height: 32px;
  padding: 0 10px;
  border-radius: var(--r2);
  border: 1px solid var(--olv);
  background: var(--sc);
  color: var(--on-sf);
  font: 400 13px/18px 'Roboto', sans-serif;
  width: 100%;
}

.cloud-double {
  display: grid;
  grid-template-columns: 1fr;
  gap: 8px;
}

.policy-actions {
  margin-top: 14px;
  display: flex;
  align-items: center;
  justify-content: space-between;
}

.policy-msg {
  font: 400 12px/16px 'Roboto', sans-serif;
  color: var(--on-sfv);
}

.save-btn {
  height: 34px;
  padding: 0 14px;
  border-radius: var(--r6);
  border: 1px solid var(--pri);
  background: var(--pri);
  color: var(--on-pri);
  font: 500 13px/18px 'Roboto', sans-serif;
}

@media (max-width: 1439px) {
  .storage-grid {
    grid-template-columns: 1fr;
  }

  .sdt-row {
    grid-template-columns: 1fr 100px 90px 90px 1fr 120px;
  }
}

@media (max-width: 768px) {
  .storage-overview {
    flex-direction: column;
    text-align: center;
  }

  .policy-grid {
    grid-template-columns: 1fr;
  }

  .trend-toolbar {
    flex-direction: column;
    align-items: stretch;
  }

  .trend-filters {
    flex-direction: column;
    align-items: stretch;
  }
}
</style>
