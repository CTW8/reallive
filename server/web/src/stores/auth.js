import { defineStore } from 'pinia'
import { ref, computed } from 'vue'
import { authApi, setAuthFailureHandler } from '../api/index.js'

export const useAuthStore = defineStore('auth', () => {
  const token = ref(localStorage.getItem('token') || '')
  const refreshToken = ref(localStorage.getItem('refreshToken') || '')
  const user = ref(JSON.parse(localStorage.getItem('user') || 'null'))

  const isLoggedIn = computed(() => !!token.value)

  setAuthFailureHandler(() => {
    logout(false)
    if (window.location.pathname !== '/login') {
      window.location.assign('/login')
    }
  })

  async function login(username, password) {
    const data = await authApi.login(username, password)
    token.value = data.token
    refreshToken.value = data.refreshToken || ''
    user.value = data.user
    localStorage.setItem('token', data.token)
    if (refreshToken.value) localStorage.setItem('refreshToken', refreshToken.value)
    localStorage.setItem('user', JSON.stringify(data.user))
    return data
  }

  async function register(username, email, password) {
    const data = await authApi.register(username, email, password)
    if (data?.token) {
      token.value = data.token
      refreshToken.value = data.refreshToken || ''
      user.value = data.user
      localStorage.setItem('token', data.token)
      if (refreshToken.value) localStorage.setItem('refreshToken', refreshToken.value)
      localStorage.setItem('user', JSON.stringify(data.user))
    }
    return data
  }

  async function logout(callApi = true) {
    if (callApi && token.value) {
      try {
        await authApi.logout()
      } catch {
      }
    }
    token.value = ''
    refreshToken.value = ''
    user.value = null
    localStorage.removeItem('token')
    localStorage.removeItem('refreshToken')
    localStorage.removeItem('user')
  }

  return { token, refreshToken, user, isLoggedIn, login, register, logout }
})
