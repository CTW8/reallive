<script setup>
import { computed, ref, onMounted, onBeforeUnmount } from 'vue'
import { useRouter, useRoute } from 'vue-router'
import { useAuthStore } from '../../stores/auth.js'

const router = useRouter()
const route = useRoute()
const auth = useAuthStore()

const sidebarCollapsed = ref(false)
const sidebarOpenMobile = ref(false)
const notificationOpen = ref(false)
const accountMenuOpen = ref(false)

const navSections = [
  {
    label: 'Main',
    items: [
      { id: 'dashboard', label: 'Dashboard', icon: 'dashboard', to: '/dashboard' },
      { id: 'alerts', label: 'Alerts', icon: 'notifications', to: '/alerts', badge: 0 },
      { id: 'playback', label: 'Playback', icon: 'history', to: '/playback' },
    ],
  },
  {
    label: 'Manage',
    items: [
      { id: 'devices', label: 'Devices', icon: 'devices_other', to: '/devices' },
      { id: 'storage', label: 'Storage', icon: 'cloud_circle', to: '/storage' },
    ],
  },
  {
    label: 'System',
    items: [
      { id: 'settings', label: 'Settings', icon: 'settings', to: '/settings' },
    ],
  },
]

const currentTitle = computed(() => String(route.meta?.title || 'Dashboard'))
const currentNavPath = computed(() => {
  if (route.name === 'Watch') return '/dashboard'
  return route.path
})
const username = computed(() => auth.user?.username || auth.user?.email || 'User')
const userInitial = computed(() => String(username.value || 'U').charAt(0).toUpperCase())

const notificationList = [
  { id: 1, title: 'Camera offline', meta: 'Warehouse East · 2 min ago' },
  { id: 2, title: 'Motion detected', meta: 'Main Gate · 5 min ago' },
  { id: 3, title: 'Storage warning', meta: 'Usage over 85% · 8 min ago' },
]

function navigateTo(path) {
  sidebarOpenMobile.value = false
  notificationOpen.value = false
  accountMenuOpen.value = false
  if (route.path !== path) {
    router.push(path)
  }
}

function isItemActive(path) {
  if (path === '/dashboard') return currentNavPath.value === '/dashboard'
  return currentNavPath.value.startsWith(path)
}

function toggleSidebarMobile() {
  sidebarOpenMobile.value = !sidebarOpenMobile.value
}

function toggleSidebarCollapsed() {
  sidebarCollapsed.value = !sidebarCollapsed.value
}

function closeOverlays() {
  notificationOpen.value = false
  accountMenuOpen.value = false
}

function handleLogout() {
  closeOverlays()
  auth.logout()
  router.push('/login')
}

function handleDocumentClick(event) {
  const target = event?.target
  if (!(target instanceof Element)) return
  if (!target.closest('.tb-right')) {
    closeOverlays()
  }
}

function handleResize() {
  if (window.innerWidth < 1024) {
    sidebarCollapsed.value = true
    sidebarOpenMobile.value = false
  }
}

onMounted(() => {
  handleResize()
  window.addEventListener('resize', handleResize)
  window.addEventListener('click', handleDocumentClick)
})

onBeforeUnmount(() => {
  window.removeEventListener('resize', handleResize)
  window.removeEventListener('click', handleDocumentClick)
})
</script>

<template>
  <div class="app-shell">
    <div class="sidebar-overlay" :class="{ show: sidebarOpenMobile }" @click="toggleSidebarMobile"></div>

    <aside class="sidebar" :class="{ collapsed: sidebarCollapsed, open: sidebarOpenMobile }">
      <div class="sb-logo">
        <div class="logo-mark"><span class="mi mif">linked_camera</span></div>
        <span class="logo-text">RealLive</span>
      </div>

      <div class="sb-nav">
        <template v-for="section in navSections" :key="section.label">
          <div class="sb-nav-label">{{ section.label }}</div>
          <div
            v-for="item in section.items"
            :key="item.id"
            class="nav-item"
            :class="{ active: isItemActive(item.to) }"
            @click="navigateTo(item.to)"
          >
            <span class="mi" :class="{ mif: isItemActive(item.to) }">{{ item.icon }}</span>
            <span class="nav-text">{{ item.label }}</span>
            <span v-if="item.badge" class="badge">{{ item.badge }}</span>
          </div>
        </template>
      </div>

      <div class="sb-footer">
        <button
          class="sb-toggle"
          type="button"
          :aria-label="sidebarCollapsed ? 'Expand sidebar' : 'Collapse sidebar'"
          :title="sidebarCollapsed ? 'Expand sidebar' : 'Collapse sidebar'"
          @click="toggleSidebarCollapsed"
        >
          <svg class="sb-toggle-icon" viewBox="0 0 24 24" aria-hidden="true">
            <path
              :d="sidebarCollapsed ? 'M9 6l6 6-6 6' : 'M15 6l-6 6 6 6'"
              fill="none"
              stroke="currentColor"
              stroke-width="2.2"
              stroke-linecap="round"
              stroke-linejoin="round"
            />
          </svg>
        </button>
      </div>
    </aside>

    <main class="main-area">
      <header class="topbar">
        <div class="tb-left">
          <div class="tb-icon menu-btn" @click.stop="toggleSidebarMobile" title="Menu">
            <span class="mi">menu</span>
          </div>
          <div class="page-title">{{ currentTitle }}</div>
        </div>

        <div class="tb-right">
          <div class="tb-search">
            <span class="mi">search</span>
            <input type="text" placeholder="Search cameras, events, devices..." />
          </div>

          <div class="tb-icon" title="Fullscreen"><span class="mi">fullscreen</span></div>

          <div class="tb-icon" title="Notifications" @click.stop="notificationOpen = !notificationOpen; accountMenuOpen = false">
            <span class="mi">notifications</span>
            <span v-if="notificationList.length" class="dot"></span>
          </div>

          <div class="tb-avatar" @click.stop="accountMenuOpen = !accountMenuOpen; notificationOpen = false">{{ userInitial }}</div>

          <div class="tb-popover" :class="{ open: notificationOpen }">
            <div class="tbp-head">
              <h4>Notifications</h4>
              <span class="tbp-link">Mark all read</span>
            </div>
            <div class="tbp-list">
              <div v-for="item in notificationList" :key="item.id" class="tbp-item">
                <div class="title">{{ item.title }}</div>
                <div class="meta">{{ item.meta }}</div>
              </div>
            </div>
          </div>

          <div class="tb-popover tb-account-menu" :class="{ open: accountMenuOpen }">
            <div class="tbp-head">
              <h4>{{ username }}</h4>
              <span class="tbp-link" @click="navigateTo('/settings')">Settings</span>
            </div>
            <div class="tbp-list">
              <div class="tbp-item" @click="navigateTo('/settings')">
                <span class="mi">person</span>
                <span>Profile</span>
              </div>
              <div class="tbp-item" @click="navigateTo('/settings')">
                <span class="mi">security</span>
                <span>Security</span>
              </div>
              <div class="tbp-item" @click="handleLogout">
                <span class="mi">logout</span>
                <span>Logout</span>
              </div>
            </div>
          </div>
        </div>
      </header>

      <section class="content">
        <slot />
      </section>
    </main>
  </div>
</template>

<style scoped>
.app-shell { width: 100%; height: 100%; display: flex; overflow: hidden; }
.sidebar-overlay { display: none; position: fixed; inset: 0; z-index: 90; background: rgba(10, 8, 16, 0.64); }
.sidebar-overlay.show { display: block; }

.sidebar {
  width: var(--sidebar-w);
  height: 100%;
  background: var(--sc);
  border-right: 1px solid var(--olv);
  display: flex;
  flex-direction: column;
  transition: width .25s ease, transform .25s ease;
  z-index: 100;
}
.sidebar.collapsed { width: var(--sidebar-collapsed-w); }

.sb-logo {
  display: flex;
  align-items: center;
  gap: 12px;
  padding: 16px 18px;
  height: var(--topbar-h);
  border-bottom: 1px solid var(--olv);
}
.logo-mark {
  width: 32px;
  height: 32px;
  border-radius: var(--r2);
  background: linear-gradient(135deg, var(--pri), var(--ter));
  display: grid;
  place-items: center;
  flex-shrink: 0;
}
.logo-mark .mi { font-size: 18px; color: #fff; }
.logo-text {
  font: 500 18px/24px 'Roboto', sans-serif;
  color: var(--on-sf);
  white-space: nowrap;
  overflow: hidden;
}
.sidebar.collapsed .logo-text { width: 0; opacity: 0; }
.sidebar.collapsed .sb-logo { justify-content: center; padding: 16px 0; }

.sb-nav { flex: 1; overflow-y: auto; padding: 12px 8px; scrollbar-width: none; }
.sb-nav::-webkit-scrollbar { display: none; }
.sb-nav-label {
  font: 500 11px/16px 'Roboto', sans-serif;
  color: var(--ol);
  text-transform: uppercase;
  letter-spacing: 1px;
  padding: 16px 12px 8px;
  white-space: nowrap;
}
.sidebar.collapsed .sb-nav-label { font-size: 0; padding: 12px 0 4px; text-align: center; }
.sidebar.collapsed .sb-nav-label::after { content: '...'; font-size: 11px; }

.nav-item {
  display: flex;
  align-items: center;
  gap: 12px;
  padding: 10px 14px;
  border-radius: var(--r2);
  cursor: pointer;
  white-space: nowrap;
  overflow: hidden;
  margin-bottom: 2px;
  position: relative;
  transition: background .15s;
}
.nav-item:hover { background: var(--sc2); }
.nav-item.active { background: var(--pri-c); color: var(--on-pri-c); }
.nav-item .mi { font-size: 22px; color: var(--on-sfv); flex-shrink: 0; }
.nav-item.active .mi { color: var(--on-pri-c); }
.nav-text {
  font: 400 14px/20px 'Roboto', sans-serif;
  overflow: hidden;
  text-overflow: ellipsis;
}
.badge {
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
.sidebar.collapsed .nav-item { justify-content: center; padding: 10px 0; }
.sidebar.collapsed .nav-text { display: none; }
.sidebar.collapsed .badge {
  right: 6px;
  top: 4px;
  min-width: 16px;
  height: 16px;
  line-height: 16px;
  font-size: 9px;
  padding: 0 4px;
}

.sb-footer { padding: 12px; border-top: 1px solid var(--olv); }
.sb-toggle {
  width: 100%;
  height: 36px;
  border: none;
  background: transparent;
  display: flex;
  align-items: center;
  justify-content: center;
  padding: 0;
  border-radius: var(--r2);
  cursor: pointer;
}
.sb-toggle:hover { background: var(--sc2); }
.sb-toggle-icon {
  width: 20px;
  height: 20px;
  color: var(--on-sfv);
}

.main-area { flex: 1; min-width: 0; display: flex; flex-direction: column; overflow: hidden; }
.topbar {
  height: var(--topbar-h);
  background: var(--sc);
  border-bottom: 1px solid var(--olv);
  padding: 0 24px;
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 12px;
}
.tb-left { display: flex; align-items: center; gap: 14px; min-width: 0; }
.page-title { font: 500 18px/24px 'Roboto', sans-serif; color: var(--on-sf); }
.breadcrumb { font: 400 12px/16px 'Roboto', sans-serif; color: var(--on-sfv); margin-top: 2px; }
.breadcrumb .sep { margin: 0 6px; color: var(--ol); }

.tb-right { display: flex; align-items: center; gap: 8px; position: relative; }
.tb-search {
  width: 240px;
  height: 36px;
  padding: 0 14px;
  border-radius: var(--r6);
  border: 1px solid var(--olv);
  background: var(--sc2);
  display: flex;
  align-items: center;
  gap: 8px;
  transition: width .2s, border-color .2s;
}
.tb-search:focus-within { width: 300px; border-color: var(--pri); }
.tb-search .mi { font-size: 18px; color: var(--on-sfv); }
.tb-search input {
  flex: 1;
  min-width: 0;
  border: none;
  outline: none;
  background: transparent;
  color: var(--on-sf);
  font: 400 13px/18px 'Roboto', sans-serif;
}
.tb-search input::placeholder { color: var(--ol); }

.tb-icon {
  width: 36px;
  height: 36px;
  border-radius: 50%;
  display: grid;
  place-items: center;
  cursor: pointer;
  position: relative;
}
.tb-icon:hover { background: var(--sc2); }
.tb-icon .mi { font-size: 20px; color: var(--on-sfv); }
.menu-btn { display: none; }
.dot {
  position: absolute;
  top: 7px;
  right: 7px;
  width: 8px;
  height: 8px;
  border-radius: 50%;
  border: 2px solid var(--sc);
  background: var(--err);
}

.tb-avatar {
  width: 32px;
  height: 32px;
  border-radius: 50%;
  background: linear-gradient(135deg, var(--pri), var(--ter));
  color: #fff;
  font: 500 13px/18px 'Roboto', sans-serif;
  display: grid;
  place-items: center;
  cursor: pointer;
  margin-left: 8px;
}

.tb-popover {
  display: none;
  position: absolute;
  top: 46px;
  right: 0;
  min-width: 280px;
  background: var(--sc);
  border: 1px solid var(--olv);
  border-radius: var(--r3);
  box-shadow: var(--e3);
  z-index: 300;
}
.tb-popover.open { display: block; }
.tbp-head {
  padding: 12px 14px;
  border-bottom: 1px solid var(--olv);
  display: flex;
  align-items: center;
  justify-content: space-between;
}
.tbp-head h4 { font: 500 13px/18px 'Roboto', sans-serif; }
.tbp-link { font: 500 11px/16px 'Roboto', sans-serif; color: var(--pri); cursor: pointer; }
.tbp-list { max-height: 320px; overflow: auto; }
.tbp-item { padding: 10px 14px; border-bottom: 1px solid var(--olv); cursor: pointer; }
.tbp-item:last-child { border-bottom: none; }
.tbp-item:hover { background: var(--sc2); }
.tbp-item .title { font: 500 12px/16px 'Roboto', sans-serif; }
.tbp-item .meta { font: 400 11px/16px 'Roboto', sans-serif; color: var(--on-sfv); margin-top: 2px; }
.tb-account-menu { min-width: 220px; }
.tb-account-menu .tbp-item { display: flex; align-items: center; gap: 8px; }
.tb-account-menu .tbp-item .mi { font-size: 17px; color: var(--on-sfv); }

.content {
  flex: 1;
  overflow-y: auto;
  overflow-x: hidden;
  padding: 24px;
  background: var(--sc0);
}
.content::-webkit-scrollbar { width: 6px; }
.content::-webkit-scrollbar-thumb { background: var(--sc3); border-radius: 3px; }

@media (max-width: 1279px) {
  .tb-search { width: 200px; }
  .tb-search:focus-within { width: 240px; }
}

@media (max-width: 1023px) {
  .menu-btn { display: grid; }
  .sidebar {
    position: fixed;
    left: 0;
    top: 0;
    bottom: 0;
    transform: translateX(-100%);
    width: var(--sidebar-w);
  }
  .sidebar.open { transform: translateX(0); }
  .sidebar.collapsed { width: var(--sidebar-w); }
  .sidebar.collapsed .logo-text { width: auto; opacity: 1; }
  .sidebar.collapsed .sb-logo { justify-content: flex-start; padding: 16px 18px; }
  .sidebar.collapsed .nav-item { justify-content: flex-start; padding: 10px 14px; }
  .sidebar.collapsed .nav-text { display: inline; }
  .sb-toggle { height: 36px; }
  .topbar { padding: 0 12px; }
  .tb-search { width: 180px; }
  .tb-search:focus-within { width: 200px; }
}

@media (max-width: 768px) {
  .tb-search { display: none; }
  .content { padding: 16px; }
  .breadcrumb { display: none; }
}
</style>
