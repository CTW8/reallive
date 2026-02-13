<script setup>
import { ref } from 'vue'
import { cameraApi } from '../../api/index.js'

const emit = defineEmits(['close', 'added'])

const name = ref('')
const resolution = ref('1080p')
const loading = ref(false)
const error = ref('')

const resolutionOptions = ['720p', '1080p', '1440p', '4K']

async function handleSubmit() {
  error.value = ''

  if (!name.value.trim()) {
    error.value = 'Camera name is required.'
    return
  }

  loading.value = true

  try {
    const result = await cameraApi.create(name.value.trim(), resolution.value)
    emit('added', result)
  } catch (err) {
    error.value = err.message || 'Failed to create camera.'
  } finally {
    loading.value = false
  }
}

function onOverlayClick(e) {
  if (e.target === e.currentTarget) {
    emit('close')
  }
}
</script>

<template>
  <div class="modal-overlay" @click="onOverlayClick">
    <div class="modal-card">
      <button class="close-btn" @click="emit('close')">&times;</button>

      <h2 class="modal-title">Add Camera</h2>

      <form @submit.prevent="handleSubmit">
        <div class="form-group">
          <label for="camera-name">Camera Name</label>
          <input
            id="camera-name"
            v-model="name"
            type="text"
            placeholder="e.g. Front Door"
            :disabled="loading"
          />
        </div>

        <div class="form-group">
          <label for="camera-resolution">Resolution</label>
          <select id="camera-resolution" v-model="resolution" :disabled="loading">
            <option v-for="opt in resolutionOptions" :key="opt" :value="opt">
              {{ opt }}
            </option>
          </select>
        </div>

        <p v-if="error" class="error-msg">{{ error }}</p>

        <div class="modal-actions">
          <button
            type="button"
            class="btn btn-secondary"
            :disabled="loading"
            @click="emit('close')"
          >
            Cancel
          </button>
          <button
            type="submit"
            class="btn btn-primary"
            :disabled="loading"
          >
            {{ loading ? 'Creating...' : 'Add Camera' }}
          </button>
        </div>
      </form>
    </div>
  </div>
</template>

<style scoped>
.modal-overlay {
  position: fixed;
  inset: 0;
  background: rgba(0, 0, 0, 0.6);
  display: flex;
  align-items: center;
  justify-content: center;
  z-index: 200;
}

.modal-card {
  background: var(--bg-card);
  border: 1px solid var(--border-color);
  border-radius: var(--radius-lg);
  padding: 32px;
  width: 100%;
  max-width: 520px;
  max-height: 90vh;
  overflow-y: auto;
  position: relative;
}

.close-btn {
  position: absolute;
  top: 16px;
  right: 16px;
  background: none;
  border: none;
  color: var(--text-secondary);
  font-size: 1.5rem;
  line-height: 1;
  padding: 4px 8px;
  border-radius: var(--radius);
  transition: color 0.2s, background 0.2s;
}

.close-btn:hover {
  color: var(--text-primary);
  background: var(--bg-hover);
}

.modal-title {
  font-size: 1.25rem;
  font-weight: 700;
  color: var(--text-primary);
  margin-bottom: 24px;
}

.modal-actions {
  display: flex;
  justify-content: flex-end;
  gap: 10px;
  margin-top: 24px;
}
</style>
