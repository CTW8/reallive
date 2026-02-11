<script setup>
import { useAuthStore } from './stores/auth.js'
import Navbar from './components/Navbar.vue'

const auth = useAuthStore()
</script>

<template>
  <div class="app">
    <Navbar v-if="auth.isLoggedIn" />
    <main class="main-content" :class="{ 'no-nav': !auth.isLoggedIn }">
      <router-view />
    </main>
  </div>
</template>

<style scoped>
.app {
  min-height: 100vh;
  display: flex;
  flex-direction: column;
}

.main-content {
  flex: 1;
  padding: 24px;
  max-width: 1400px;
  width: 100%;
  margin: 0 auto;
}

.main-content.no-nav {
  display: flex;
  align-items: center;
  justify-content: center;
  padding: 0;
  max-width: none;
}
</style>
