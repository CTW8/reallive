export function heartbeatLabelFromTs(ts) {
  const t = Number(ts || 0)
  if (!Number.isFinite(t) || t <= 0) return 'STALE'
  const diffSec = Math.max(0, Math.floor((Date.now() - t) / 1000))
  if (diffSec <= 5) return 'GOOD'
  if (diffSec <= 15) return 'WEAK'
  return 'STALE'
}

export function heartbeatClassFromLabel(label, prefix = 'hb-') {
  const normalized = String(label || '').toUpperCase()
  if (normalized === 'GOOD') return `${prefix}good`
  if (normalized === 'WEAK') return `${prefix}weak`
  return `${prefix}stale`
}

export function heartbeatTsFromRuntime(runtime, fallbackTs = 0) {
  const runtimeTs = Number(runtime?.updatedAt || runtime?.ts || 0)
  if (Number.isFinite(runtimeTs) && runtimeTs > 0) return runtimeTs
  const backup = Number(fallbackTs || 0)
  if (Number.isFinite(backup) && backup > 0) return backup
  return 0
}

export function formatUpdatedAgo(ts) {
  const t = Number(ts || 0)
  if (!Number.isFinite(t) || t <= 0) return 'No telemetry'
  const diffSec = Math.max(0, Math.floor((Date.now() - t) / 1000))
  if (diffSec < 5) return 'Updated now'
  if (diffSec < 60) return `Updated ${diffSec}s ago`
  const mins = Math.floor(diffSec / 60)
  if (mins < 60) return `Updated ${mins}m ago`
  const hours = Math.floor(mins / 60)
  if (hours < 24) return `Updated ${hours}h ago`
  return `Updated ${Math.floor(hours / 24)}d ago`
}
