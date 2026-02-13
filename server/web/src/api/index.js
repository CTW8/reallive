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
  const data = await res.json()

  if (!res.ok) {
    throw new Error(data.error || `Request failed: ${res.status}`)
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
