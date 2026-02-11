import { defineStore } from 'pinia'
import { ref } from 'vue'
import { cameraApi } from '../api/index.js'

export const useCameraStore = defineStore('camera', () => {
  const cameras = ref([])
  const loading = ref(false)

  async function fetchCameras() {
    loading.value = true
    try {
      const data = await cameraApi.list()
      cameras.value = data.cameras || data
    } finally {
      loading.value = false
    }
  }

  async function addCamera(name, resolution) {
    const data = await cameraApi.create(name, resolution || '1080p')
    cameras.value.push(data.camera || data)
    return data
  }

  async function updateCamera(id, updates) {
    const data = await cameraApi.update(id, updates)
    const idx = cameras.value.findIndex((c) => c.id === id)
    if (idx !== -1) {
      cameras.value[idx] = { ...cameras.value[idx], ...updates }
    }
    return data
  }

  async function removeCamera(id) {
    await cameraApi.remove(id)
    cameras.value = cameras.value.filter((c) => c.id !== id)
  }

  function updateCameraStatus(cameraId, status) {
    const cam = cameras.value.find((c) => c.id === cameraId)
    if (cam) cam.status = status
  }

  return { cameras, loading, fetchCameras, addCamera, updateCamera, removeCamera, updateCameraStatus }
})
