<script setup>
import { computed } from 'vue'
import { useRoute } from 'vue-router'
import { useAuthStore } from './stores/auth.js'
import AppShell from './components/layout/AppShell.vue'
import './styles/global.css'

const route = useRoute()
const auth = useAuthStore()

const needsShell = computed(() => {
  return auth.isLoggedIn && !['Login', 'Register'].includes(route.name)
})
</script>

<template>
  <AppShell v-if="needsShell">
    <router-view />
  </AppShell>
  <router-view v-else />
</template>

<style>
#app {
  width: 100%;
  height: 100%;
}
</style>
