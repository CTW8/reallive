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
