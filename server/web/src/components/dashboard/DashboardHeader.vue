<script setup>
defineProps({
  cameraCount: { type: Number, default: 0 },
})

const emit = defineEmits(['add-camera'])
</script>

<template>
  <div class="dashboard-header">
    <div class="header-title">
      <h1>Dashboard</h1>
      <p class="header-subtitle">{{ cameraCount }} camera{{ cameraCount === 1 ? '' : 's' }} configured</p>
    </div>
    <button class="btn btn-primary" @click="emit('add-camera')">
      + Add Camera
    </button>
  </div>
</template>

<style scoped>
.dashboard-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 16px;
}

.header-title h1 {
  font-size: 1.5rem;
  font-weight: 700;
  color: var(--text-primary);
  line-height: 1.3;
}

.header-subtitle {
  font-size: 0.88rem;
  color: var(--text-secondary);
  margin-top: 2px;
}
</style>
