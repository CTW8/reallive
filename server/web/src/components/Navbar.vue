<script setup>
import { useRouter } from 'vue-router'
import { useAuthStore } from '../stores/auth.js'
import { disconnectSignaling } from '../api/signaling.js'

const router = useRouter()
const auth = useAuthStore()

function handleLogout() {
  disconnectSignaling()
  auth.logout()
  router.push('/login')
}
</script>

<template>
  <nav class="navbar">
    <div class="navbar-inner">
      <router-link to="/" class="navbar-brand">
        <span class="brand-icon">&#9673;</span>
        RealLive
      </router-link>
      <div class="navbar-links">
        <router-link to="/" class="nav-link" exact-active-class="active">Dashboard</router-link>
        <router-link to="/multiview" class="nav-link" active-class="active">Multi View</router-link>
      </div>
      <div class="navbar-right">
        <span class="user-name">{{ auth.user?.username }}</span>
        <button class="btn btn-secondary btn-sm" @click="handleLogout">Logout</button>
      </div>
    </div>
  </nav>
</template>

<style scoped>
.navbar {
  background: var(--bg-secondary);
  border-bottom: 1px solid var(--border-color);
  padding: 0 24px;
  position: sticky;
  top: 0;
  z-index: 100;
}

.navbar-inner {
  max-width: 1400px;
  margin: 0 auto;
  display: flex;
  align-items: center;
  height: 56px;
  gap: 32px;
}

.navbar-brand {
  display: flex;
  align-items: center;
  gap: 8px;
  font-size: 1.1rem;
  font-weight: 700;
  color: var(--text-primary);
  letter-spacing: 0.5px;
}

.brand-icon {
  font-size: 1.4rem;
  color: var(--accent);
}

.navbar-links {
  display: flex;
  gap: 4px;
}

.nav-link {
  padding: 8px 16px;
  border-radius: var(--radius);
  color: var(--text-secondary);
  font-size: 0.9rem;
  font-weight: 500;
  transition: color 0.2s, background 0.2s;
}

.nav-link:hover {
  color: var(--text-primary);
  background: var(--bg-hover);
}

.nav-link.active {
  color: var(--accent);
  background: rgba(76, 123, 244, 0.1);
}

.navbar-right {
  margin-left: auto;
  display: flex;
  align-items: center;
  gap: 12px;
}

.user-name {
  font-size: 0.85rem;
  color: var(--text-secondary);
}
</style>
