import { createRouter, createWebHistory } from 'vue-router'
import { useAuthStore } from '../stores/auth.js'

const routes = [
  {
    path: '/login',
    name: 'Login',
    component: () => import('../views/LoginView.vue'),
    meta: { guest: true },
  },
  {
    path: '/register',
    name: 'Register',
    component: () => import('../views/RegisterView.vue'),
    meta: { guest: true },
  },
  {
    path: '/',
    name: 'Dashboard',
    component: () => import('../views/DashboardView.vue'),
    meta: { auth: true },
  },
  {
    path: '/monitor',
    name: 'Monitor',
    component: () => import('../views/monitor/MonitorView.vue'),
    meta: { auth: true },
  },
  {
    path: '/playback',
    name: 'Playback',
    component: () => import('../views/playback/PlaybackView.vue'),
    meta: { auth: true },
  },
  {
    path: '/alerts',
    name: 'Alerts',
    component: () => import('../views/alerts/AlertsView.vue'),
    meta: { auth: true },
  },
  {
    path: '/devices',
    name: 'Devices',
    component: () => import('../views/devices/DevicesView.vue'),
    meta: { auth: true },
  },
  {
    path: '/storage',
    name: 'Storage',
    component: () => import('../views/storage/StorageView.vue'),
    meta: { auth: true },
  },
  {
    path: '/settings',
    name: 'Settings',
    component: () => import('../views/settings/SettingsView.vue'),
    meta: { auth: true },
  },
  {
    path: '/watch/:id',
    name: 'Watch',
    component: () => import('../views/WatchView.vue'),
    meta: { auth: true },
  },
]

const router = createRouter({
  history: createWebHistory(),
  routes,
})

router.beforeEach((to) => {
  const auth = useAuthStore()
  if (to.meta.auth && !auth.isLoggedIn) {
    return { name: 'Login' }
  }
  if (to.meta.guest && auth.isLoggedIn) {
    return { name: 'Dashboard' }
  }
})

export default router
