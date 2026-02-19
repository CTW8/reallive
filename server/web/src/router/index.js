import { createRouter, createWebHistory } from 'vue-router'
import { useAuthStore } from '../stores/auth.js'

const routes = [
  {
    path: '/',
    redirect: '/dashboard',
  },
  {
    path: '/login',
    name: 'Login',
    component: () => import('../views/LoginView.vue'),
    meta: { guest: true, title: 'Login' },
  },
  {
    path: '/register',
    name: 'Register',
    component: () => import('../views/RegisterView.vue'),
    meta: { guest: true, title: 'Register' },
  },
  {
    path: '/dashboard',
    name: 'Dashboard',
    component: () => import('../views/DashboardView.vue'),
    meta: { auth: true, title: 'Dashboard', section: 'Overview' },
  },
  {
    path: '/monitor',
    name: 'Monitor',
    component: () => import('../views/monitor/MonitorView.vue'),
    meta: { auth: true, title: 'Live View', section: 'Main' },
  },
  {
    path: '/playback',
    name: 'Playback',
    component: () => import('../views/playback/PlaybackView.vue'),
    meta: { auth: true, title: 'Playback', section: 'Main' },
  },
  {
    path: '/alerts',
    name: 'Alerts',
    component: () => import('../views/alerts/AlertsView.vue'),
    meta: { auth: true, title: 'Alerts', section: 'Main' },
  },
  {
    path: '/devices',
    name: 'Devices',
    component: () => import('../views/devices/DevicesView.vue'),
    meta: { auth: true, title: 'Devices', section: 'Manage' },
  },
  {
    path: '/storage',
    name: 'Storage',
    component: () => import('../views/storage/StorageView.vue'),
    meta: { auth: true, title: 'Storage', section: 'Manage' },
  },
  {
    path: '/settings',
    name: 'Settings',
    component: () => import('../views/settings/SettingsView.vue'),
    meta: { auth: true, title: 'Settings', section: 'System' },
  },
  {
    path: '/watch/:id',
    name: 'Watch',
    component: () => import('../views/monitor/MonitorView.vue'),
    meta: { auth: true, title: 'Live View', section: 'Main', hiddenInNav: true },
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
    return { path: '/dashboard' }
  }
})

export default router
