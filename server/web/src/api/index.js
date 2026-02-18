const BASE_URL = '/api'

function getToken() {
  return localStorage.getItem('token')
}

async function request(path, options = {}) {
  const headers = { 'Content-Type': 'application/json', ...options.headers }
  const token = getToken()
  if (token) {
    headers['Authorization'] = `Bearer ${token}`
  }

  const res = await fetch(`${BASE_URL}${path}`, { ...options, headers })
  const contentType = res.headers.get('content-type') || ''
  let data = null

  if (contentType.includes('application/json')) {
    try {
      data = await res.json()
    } catch {
      data = null
    }
  } else {
    try {
      const text = await res.text()
      data = text ? { error: text } : null
    } catch {
      data = null
    }
  }

  if (!res.ok) {
    if (res.status === 401) {
      localStorage.removeItem('token')
      localStorage.removeItem('user')
    }
    const err = new Error(data?.error || `Request failed: ${res.status}`)
    err.status = res.status
    throw err
  }
  return data
}

export const authApi = {
  login(username, password) {
    return request('/auth/login', {
      method: 'POST',
      body: JSON.stringify({ username, password }),
    })
  },
  register(username, email, password) {
    return request('/auth/register', {
      method: 'POST',
      body: JSON.stringify({ username, email, password }),
    })
  },
}

export const cameraApi = {
  list() {
    return request('/cameras')
  },
  create(name, resolution) {
    return request('/cameras', {
      method: 'POST',
      body: JSON.stringify({ name, resolution }),
    })
  },
  update(id, data) {
    return request(`/cameras/${id}`, {
      method: 'PUT',
      body: JSON.stringify(data),
    })
  },
  remove(id) {
    return request(`/cameras/${id}`, { method: 'DELETE' })
  },
  getStreamInfo(id) {
    return request(`/cameras/${id}/stream`)
  },
  startWatchSession(id) {
    return request(`/cameras/${id}/watch/start`, {
      method: 'POST',
      body: JSON.stringify({}),
    })
  },
  heartbeatWatchSession(id, sessionId) {
    return request(`/cameras/${id}/watch/heartbeat`, {
      method: 'POST',
      body: JSON.stringify({ sessionId }),
    })
  },
  stopWatchSession(id, sessionId) {
    return request(`/cameras/${id}/watch/stop`, {
      method: 'POST',
      body: JSON.stringify({ sessionId }),
    })
  },
  getHistoryOverview(id) {
    return request(`/cameras/${id}/history/overview`)
  },
  getHistoryTimeline(id, params = {}) {
    const query = new URLSearchParams()
    if (params.start != null) query.set('start', String(params.start))
    if (params.end != null) query.set('end', String(params.end))
    return request(`/cameras/${id}/history/timeline?${query.toString()}`)
  },
  getHistoryPlayback(id, ts) {
    const query = new URLSearchParams()
    if (ts != null) query.set('ts', String(ts))
    return request(`/cameras/${id}/history/play?${query.toString()}`)
  },
  stopHistoryReplay(id, sessionId = null) {
    return request(`/cameras/${id}/history/replay/stop`, {
      method: 'POST',
      body: JSON.stringify({ sessionId }),
    })
  },
}

export const dashboardApi = {
  getStats() {
    return request('/dashboard/stats')
  },
  getHealth() {
    return request('/health')
  },
}

export const sessionApi = {
  list(limit = 20, offset = 0) {
    return request(`/sessions?limit=${limit}&offset=${offset}`)
  },
  getActive() {
    return request('/sessions/active')
  },
}

export const alertApi = {
  list(filters = {}) {
    const query = new URLSearchParams()
    if (filters.type) query.set('type', filters.type)
    if (filters.status) query.set('status', filters.status)
    if (filters.limit) query.set('limit', filters.limit)
    const qs = query.toString()
    return request(`/alerts${qs ? '?' + qs : ''}`)
  },
  getStats() {
    return request('/alerts/stats')
  },
  getUnreadCount() {
    return request('/alerts/unread-count')
  },
  get(id) {
    return request(`/alerts/${id}`)
  },
  create(data) {
    return request('/alerts', {
      method: 'POST',
      body: JSON.stringify(data),
    })
  },
  update(id, data) {
    return request(`/alerts/${id}`, {
      method: 'PATCH',
      body: JSON.stringify(data),
    })
  },
  markRead(id) {
    return request(`/alerts/${id}/read`, { method: 'POST' })
  },
  resolve(id) {
    return request(`/alerts/${id}/resolve`, { method: 'POST' })
  },
  batchAction(ids, action) {
    return request('/alerts/batch', {
      method: 'POST',
      body: JSON.stringify({ ids, action }),
    })
  },
  delete(id) {
    return request(`/alerts/${id}`, { method: 'DELETE' })
  },
}

export const ruleApi = {
  list(filters = {}) {
    const query = new URLSearchParams()
    if (filters.priority) query.set('priority', filters.priority)
    if (filters.enabled) query.set('enabled', filters.enabled)
    if (filters.escalation) query.set('escalation', filters.escalation)
    if (filters.query) query.set('query', filters.query)
    if (filters.sortBy) query.set('sortBy', filters.sortBy)
    if (filters.order) query.set('order', filters.order)
    const qs = query.toString()
    return request(`/alert-rules${qs ? '?' + qs : ''}`)
  },
  get(id) {
    return request(`/alert-rules/${id}`)
  },
  create(data) {
    return request('/alert-rules', {
      method: 'POST',
      body: JSON.stringify(data),
    })
  },
  update(id, data) {
    return request(`/alert-rules/${id}`, {
      method: 'PATCH',
      body: JSON.stringify(data),
    })
  },
  toggle(id) {
    return request(`/alert-rules/${id}/toggle`, { method: 'POST' })
  },
  batchAction(ids, action) {
    return request('/alert-rules/batch', {
      method: 'POST',
      body: JSON.stringify({ ids, action }),
    })
  },
  delete(id) {
    return request(`/alert-rules/${id}`, { method: 'DELETE' })
  },
}

export const storageApi = {
  getOverview() {
    return request('/storage/overview')
  },
  getTrend(days = 14) {
    return request(`/storage/trend?days=${days}`)
  },
  getByDevice() {
    return request('/storage/by-device')
  },
  updatePolicy(data) {
    return request('/storage/policy', {
      method: 'POST',
      body: JSON.stringify(data),
    })
  },
}
