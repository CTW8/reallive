<script setup>
import { ref, computed, onMounted, onBeforeUnmount } from 'vue'
import { useRouter, useRoute } from 'vue-router'
import { useAuthStore } from '../../stores/auth.js'

const router = useRouter()
const route = useRoute()
const auth = useAuthStore()

const sidebarCollapsed = ref(false)
const sidebarOpenMobile = ref(false)

const navItems = [
  { id: 'dashboard', label: 'Dashboard', icon: 'dashboard', section: 'Monitor' },
  { id: 'monitor', label: 'Live Monitor', icon: 'videocam', section: 'Monitor' },
  { id: 'playback', label: 'Playback', icon: 'history', section: 'Monitor' },
  { id: 'alerts', label: 'Alert Center', icon: 'notifications', section: 'Monitor', badge: 5 },
  { id: 'devices', label: 'Devices', icon: 'devices_other', section: 'Manage' },
  { id: 'storage', label: 'Storage', icon: 'cloud_circle', section: 'Manage' },
  { id: 'settings', label: 'Settings', icon: 'settings', section: 'System' },
]

const currentPage = computed(() => {
  const path = route.path.split('/')[1] || 'dashboard'
  return navItems.find(item => item.id === path) || navItems[0]
})

function navigateTo(id) {
  sidebarOpenMobile.value = false
  router.push(`/${id}`)
}

function toggleSidebar() {
  sidebarOpenMobile.value = !sidebarOpenMobile.value
}

function collapseSidebar() {
  sidebarCollapsed.value = !sidebarCollapsed.value
}

function handleResize() {
  if (window.innerWidth < 1024) {
    sidebarCollapsed.value = true
  }
}

onMounted(() => {
  window.addEventListener('resize', handleResize)
  handleResize()
})

onBeforeUnmount(() => {
  window.removeEventListener('resize', handleResize)
})

function handleLogout() {
  auth.logout()
  router.push('/login')
}
</script>

<template>
  <div class="app-shell">
    <div
      class="sidebar-overlay"
      :class="{ show: sidebarOpenMobile }"
      @click="toggleSidebar"
    ></div>

    <nav class="sidebar" :class="{ collapsed: sidebarCollapsed, open: sidebarOpenMobile }">
      <div class="sb-logo">
        <div class="logo-mark"><span class="mi mif">linked_camera</span></div>
        <span class="logo-text">RealLive</span>
      </div>

      <div class="sb-nav">
        <template v-for="section in ['Monitor', 'Manage', 'System']" :key="section">
          <div class="sb-nav-label">{{ section }}</div>
          <div
            v-for="item in navItems.filter(i => i.section === section)"
            :key="item.id"
            class="nav-item"
            :class="{ active: currentPage.id === item.id }"
            @click="navigateTo(item.id)"
          >
            <span class="mi" :class="{ mif: currentPage.id === item.id }">{{ item.icon }}</span>
            <span class="nav-text">{{ item.label }}</span>
            <span v-if="item.badge" class="badge">{{ item.badge }}</span>
          </div>
        </template>
      </div>

      <div class="sb-footer">
        <div class="sb-toggle" @click="collapseSidebar">
          <span class="mi">menu_open</span>
          <span>{{ sidebarCollapsed ? 'Expand' : 'Collapse' }}</span>
        </div>
      </div>
    </nav>

    <div class="main-area">
      <header class="topbar">
        <div class="tb-left">
          <span
            class="mi menu-btn"
            style="font-size:24px;cursor:pointer"
            @click="toggleSidebar"
          >menu</span>
          <span class="page-title">{{ currentPage.label }}</span>
        </div>
        <div class="tb-right">
          <div class="tb-search">
            <span class="mi">search</span>
            <input type="text" placeholder="Search cameras, events...">
          </div>
          <div class="tb-icon" title="Fullscreen">
            <span class="mi">fullscreen</span>
          </div>
          <div class="tb-icon" title="Dark mode">
            <span class="mi">dark_mode</span>
          </div>
          <div class="tb-icon" title="Notifications">
            <span class="mi">notifications</span>
            <span class="dot"></span>
          </div>
          <div class="tb-avatar" @click="handleLogout">{{ auth.user?.email?.[0]?.toUpperCase() || 'U' }}</div>
        </div>
      </header>

      <div class="content">
        <slot></slot>
      </div>
    </div>
  </div>
</template>

<style scoped>
.app-shell {
  width: 100%;
  height: 100%;
  display: flex;
  overflow: hidden;
}

.sidebar-overlay {
  display: none;
  position: fixed;
  inset: 0;
  background: rgba(0,0,0,.5);
  z-index: 999;
}

.sidebar-overlay.show {
  display: block;
}

.sidebar {
  width: var(--sidebar-w);
  height: 100%;
  background: var(--sc);
  display: flex;
  flex-direction: column;
  flex-shrink: 0;
  border-right: 1px solid var(--olv);
  transition: width .25s ease;
  overflow: hidden;
  z-index: 100;
}

.sidebar.collapsed {
  width: var(--sidebar-collapsed-w);
}

.sb-logo {
  display: flex;
  align-items: center;
  gap: 12px;
  padding: 16px 18px;
  height: var(--topbar-h);
  flex-shrink: 0;
  border-bottom: 1px solid var(--olv);
}

.logo-mark {
  width: 32px;
  height: 32px;
  border-radius: var(--r2);
  background: linear-gradient(135deg, var(--pri), var(--ter));
  display: flex;
  align-items: center;
  justify-content: center;
  flex-shrink: 0;
}

.logo-mark .mi {
  font-size: 18px;
  color: #fff;
}

.logo-text {
  font: 500 18px/24px 'Roboto', sans-serif;
  color: var(--on-sf);
  white-space: nowrap;
  overflow: hidden;
}

.sidebar.collapsed .logo-text {
  opacity: 0;
  width: 0;
}

.sidebar.collapsed .sb-logo {
  justify-content: center;
  padding: 16px 0;
}

.sb-nav {
  flex: 1;
  padding: 12px 8px;
  overflow-y: auto;
  scrollbar-width: none;
}

.sb-nav::-webkit-scrollbar {
  display: none;
}

.sb-nav-label {
  font: 500 11px/16px 'Roboto', sans-serif;
  color: var(--ol);
  text-transform: uppercase;
  letter-spacing: 1px;
  padding: 16px 12px 8px;
  white-space: nowrap;
  overflow: hidden;
}

.sidebar.collapsed .sb-nav-label {
  text-align: center;
  font-size: 0;
  padding: 12px 0 4px;
}

.sidebar.collapsed .sb-nav-label::after {
  content: '...';
  font-size: 11px;
}

.nav-item {
  display: flex;
  align-items: center;
  gap: 12px;
  padding: 10px 14px;
  border-radius: var(--r2);
  cursor: pointer;
  transition: background .15s;
  position: relative;
  margin-bottom: 2px;
  white-space: nowrap;
  overflow: hidden;
}

.nav-item:hover {
  background: var(--sc2);
}

.nav-item.active {
  background: var(--pri-c);
  color: var(--on-pri-c);
}

.nav-item.active .mi {
  color: var(--on-pri-c);
}

.nav-item .mi {
  font-size: 22px;
  color: var(--on-sfv);
  flex-shrink: 0;
}

.nav-item .nav-text {
  font: 400 14px/20px 'Roboto', sans-serif;
  overflow: hidden;
  text-overflow: ellipsis;
}

.nav-item .badge {
  position: absolute;
  right: 12px;
  min-width: 20px;
  height: 20px;
  border-radius: 10px;
  background: var(--err-c);
  color: #fff;
  font: 500 11px/20px 'Roboto', sans-serif;
  text-align: center;
  padding: 0 6px;
}

.sidebar.collapsed .nav-item {
  justify-content: center;
  padding: 10px 0;
}

.sidebar.collapsed .nav-item .nav-text {
  display: none;
}

.sidebar.collapsed .nav-item .badge {
  right: 6px;
  top: 4px;
  min-width: 16px;
  height: 16px;
  font-size: 9px;
  line-height: 16px;
  padding: 0 4px;
}

.sb-footer {
  padding: 12px;
  border-top: 1px solid var(--olv);
  flex-shrink: 0;
}

.sb-toggle {
  display: flex;
  align-items: center;
  justify-content: center;
  gap: 10px;
  padding: 8px;
  border-radius: var(--r2);
  cursor: pointer;
  transition: background .15s;
  white-space: nowrap;
  overflow: hidden;
}

.sb-toggle:hover {
  background: var(--sc2);
}

.sb-toggle .mi {
  font-size: 20px;
  color: var(--on-sfv);
  flex-shrink: 0;
}

.sb-toggle span {
  font: 400 13px/18px 'Roboto', sans-serif;
  color: var(--on-sfv);
  overflow: hidden;
}

.sidebar.collapsed .sb-toggle span {
  display: none;
}

.main-area {
  flex: 1;
  display: flex;
  flex-direction: column;
  min-width: 0;
  overflow: hidden;
}

.topbar {
  height: var(--topbar-h);
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 0 24px;
  background: var(--sc);
  border-bottom: 1px solid var(--olv);
  flex-shrink: 0;
}

.tb-left {
  display: flex;
  align-items: center;
  gap: 16px;
}

.menu-btn {
  display: none;
}

.page-title {
  font: 500 18px/24px 'Roboto', sans-serif;
}

.tb-right {
  display: flex;
  align-items: center;
  gap: 8px;
}

.tb-search {
  display: flex;
  align-items: center;
  gap: 8px;
  height: 36px;
  padding: 0 14px;
  border-radius: var(--r6);
  background: var(--sc2);
  border: 1px solid var(--olv);
  width: 240px;
  transition: width .2s;
}

.tb-search:focus-within {
  border-color: var(--pri);
  width: 300px;
}

.tb-search .mi {
  font-size: 18px;
  color: var(--on-sfv);
}

.tb-search input {
  flex: 1;
  border: none;
  background: transparent;
  color: var(--on-sf);
  font: 400 13px/18px 'Roboto', sans-serif;
  outline: none;
  min-width: 0;
}

.tb-search input::placeholder {
  color: var(--ol);
}

.tb-icon {
  width: 36px;
  height: 36px;
  border-radius: 50%;
  display: flex;
  align-items: center;
  justify-content: center;
  cursor: pointer;
  transition: background .15s;
  position: relative;
}

.tb-icon:hover {
  background: var(--sc2);
}

.tb-icon .mi {
  font-size: 20px;
  color: var(--on-sfv);
}

.tb-icon .dot {
  position: absolute;
  top: 6px;
  right: 6px;
  width: 8px;
  height: 8px;
  border-radius: 50%;
  background: var(--err);
  border: 2px solid var(--sc);
}

.tb-avatar {
  width: 32px;
  height: 32px;
  border-radius: 50%;
  background: linear-gradient(135deg, var(--pri), var(--ter));
  display: flex;
  align-items: center;
  justify-content: center;
  font: 500 13px/18px 'Roboto', sans-serif;
  color: #fff;
  cursor: pointer;
  margin-left: 8px;
}

.content {
  flex: 1;
  overflow-y: auto;
  overflow-x: hidden;
  padding: 24px;
  background: var(--sc0);
}

.content::-webkit-scrollbar {
  width: 6px;
}

.content::-webkit-scrollbar-track {
  background: transparent;
}

.content::-webkit-scrollbar-thumb {
  background: var(--sc3);
  border-radius: 3px;
}

@media (max-width: 1023px) {
  .sidebar {
    position: fixed;
    left: -260px;
    z-index: 1000;
    transition: left .25s;
  }

  .sidebar.open {
    left: 0;
  }

  .menu-btn {
    display: block;
  }
}
</style>
