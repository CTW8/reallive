function toSafeNumber(value, fallback = 0) {
  const n = Number(value)
  return Number.isFinite(n) ? n : fallback
}

function formatRelativeTime(timestamp) {
  if (!timestamp) return 'just now'
  const ts = new Date(timestamp).getTime()
  if (!Number.isFinite(ts)) return 'just now'
  const diffMs = Date.now() - ts
  const min = Math.floor(diffMs / 60000)
  if (min <= 1) return 'just now'
  if (min < 60) return `${min} minutes ago`
  const hr = Math.floor(min / 60)
  if (hr < 24) return `${hr} hours ago`
  const day = Math.floor(hr / 24)
  return `${day} days ago`
}

function mapEventType(eventType) {
  const type = String(eventType || '').toLowerCase()
  if (type.includes('person') || type.includes('motion')) {
    return { uiType: 'motion', title: 'Motion Detected', icon: 'directions_run' }
  }
  if (type.includes('stop') || type.includes('offline')) {
    return { uiType: 'alarm', title: 'Camera Offline', icon: 'warning' }
  }
  if (type.includes('start') || type.includes('online')) {
    return { uiType: 'info', title: 'Stream Started', icon: 'play_circle' }
  }
  return { uiType: 'info', title: 'System Event', icon: 'info' }
}

function buildAlertsFromEvents(events = []) {
  const out = []
  for (const evt of events) {
    const mapped = mapEventType(evt?.type)
    out.push({
      id: evt?.id || `${evt?.cameraId || 'c'}-${evt?.timestamp || Date.now()}-${evt?.type || 'evt'}`,
      type: mapped.uiType,
      icon: mapped.icon,
      title: mapped.title,
      desc: `${evt?.cameraName || 'Camera'} - ${evt?.type || 'activity'} event`,
      time: formatRelativeTime(evt?.timestamp),
      ts: new Date(evt?.timestamp || Date.now()).getTime(),
    })
  }
  return out.sort((a, b) => b.ts - a.ts)
}

function buildFallbackAlerts(cameras = []) {
  const offline = cameras.filter((c) => c.status === 'offline').map((camera) => ({
    id: `offline-${camera.id}`,
    type: 'alarm',
    icon: 'warning',
    title: 'Camera Offline',
    desc: `${camera.name} - Connection unavailable`,
    time: 'just now',
    ts: Date.now(),
  }))

  if (offline.length) return offline

  return [{
    id: 'system-ok',
    type: 'info',
    icon: 'info',
    title: 'System Normal',
    desc: 'No recent alert events received',
    time: 'just now',
    ts: Date.now(),
  }]
}

function createMapDevices(cameras = []) {
  const coords = [
    [22, 22], [52, 18], [76, 26], [35, 48], [62, 52], [18, 70], [49, 72], [80, 68],
  ]

  return cameras.slice(0, 8).map((camera, index) => {
    const [left, top] = coords[index] || [15 + (index * 8) % 70, 18 + (index * 7) % 62]
    return {
      id: camera.id,
      name: camera.name,
      status: camera.status,
      leftPct: left,
      topPct: top,
      zone: index % 3 === 0 ? 'entry' : (index % 3 === 1 ? 'perimeter' : 'critical'),
      location: camera.resolution || 'Default',
    }
  })
}

function computeTrendSeries(stats, alertsCount) {
  const base = Math.max(1, toSafeNumber(stats?.cameras?.online, 1))
  const alertBase = Math.max(1, alertsCount)
  const labels = ['Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat', 'Sun']
  return labels.map((label, i) => ({
    label,
    primary: Math.max(8, Math.min(100, Math.round((base * (0.72 + i * 0.05)) % 100))),
    secondary: Math.max(8, Math.min(100, Math.round((alertBase * (0.55 + i * 0.04) * 11) % 100))),
  }))
}

export function adaptDashboardViewModel({ stats, cameras, activityEvents }) {
  const safeStats = stats || {}
  const safeCameras = Array.isArray(cameras) ? cameras : []
  const safeEvents = Array.isArray(activityEvents) ? activityEvents : []

  const recentAlerts = buildAlertsFromEvents(safeEvents)
  const alerts = recentAlerts.length ? recentAlerts.slice(0, 8) : buildFallbackAlerts(safeCameras)

  const online = toSafeNumber(safeStats?.cameras?.online)
  const offline = toSafeNumber(safeStats?.cameras?.offline)
  const streaming = toSafeNumber(safeStats?.cameras?.streaming)
  const total = toSafeNumber(safeStats?.cameras?.total, safeCameras.length)

  const mapDevices = createMapDevices(safeCameras)
  const mapSummary = {
    total: mapDevices.length,
    online: mapDevices.filter((d) => d.status !== 'offline').length,
    offline: mapDevices.filter((d) => d.status === 'offline').length,
    recording: mapDevices.filter((d) => d.status === 'streaming').length,
  }

  return {
    statsCards: [
      { key: 'online', title: 'Online Cameras', value: online, icon: 'videocam', iconClass: 'green', trendClass: 'up', trend: `+${Math.max(1, Math.round(online * 0.08))} today` },
      { key: 'offline', title: 'Offline Cameras', value: offline, icon: 'videocam_off', iconClass: 'red', trendClass: 'down', trend: `${offline > 0 ? '+' : ''}${offline} today` },
      { key: 'alerts', title: 'Active Alerts', value: alerts.length, icon: 'warning', iconClass: 'orange', trendClass: alerts.length > 3 ? 'down' : 'up', trend: `${alerts.length} in queue` },
      { key: 'total', title: 'Total Cameras', value: total, icon: 'camera', iconClass: 'purple', trendClass: 'up', trend: `${streaming} recording` },
    ],
    recentAlerts: alerts,
    mapDevices,
    mapSummary,
    trendSeries: computeTrendSeries(safeStats, alerts.length),
  }
}
