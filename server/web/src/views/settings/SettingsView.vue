<script setup>
import { computed, onMounted, ref } from 'vue'
import { useAuthStore } from '../../stores/auth.js'
import { useCameraStore } from '../../stores/camera.js'
import { dashboardApi, sessionApi, settingsApi } from '../../api/index.js'

const auth = useAuthStore()
const cameraStore = useCameraStore()

const activeTab = ref('profile')
const notice = ref('')
const loading = ref(false)
const saving = ref(false)

const tabs = [
  { id: 'profile', label: 'Profile', icon: 'person' },
  { id: 'security', label: 'Security', icon: 'security' },
  { id: 'users', label: 'Users & Roles', icon: 'groups' },
  { id: 'audit', label: 'Audit Logs', icon: 'history' },
  { id: 'notifications', label: 'Notifications', icon: 'notifications' },
  { id: 'system', label: 'System', icon: 'settings_applications' },
]

const profileForm = ref({
  displayName: '',
  email: '',
  phone: '+1 408 555 0199',
  role: 'Administrator',
  signature: 'Security operations center admin. Responsible for 24/7 monitoring and incident response.',
  language: 'English',
  timezone: 'UTC-08:00',
})

const security = ref({
  twoFactor: true,
  trustedDevice: true,
  ipAllowlist: true,
})

const passwordForm = ref({
  currentPassword: '',
  newPassword: '',
  confirmPassword: '',
})

const notificationCfg = ref({
  email: true,
  sms: true,
  webhook: false,
  quietHours: '22:00 - 06:00',
  escalationDelay: 'After 60 seconds',
  escalationRule: 'If an alert remains unresolved for 60 seconds, escalate to on-call administrator by SMS and webhook.',
})

const systemCfg = ref({
  nvrMode: 'Event Priority',
  networkProbe: true,
})

const activeSessions = ref([])
const health = ref(null)
const canManageUsers = ref(false)
const userModalOpen = ref(false)
const userModalMode = ref('create')
const userModalStep = ref(1)
const userModalError = ref('')
const revokeDrawerOpen = ref(false)
const revokeTarget = ref(null)
const userForm = ref({
  id: null,
  username: '',
  email: '',
  role: 'viewer',
  password: '',
})

const users = ref([])
const userQuery = ref('')
const userRoleFilter = ref('all')
const userPage = ref(1)
const userPageSize = ref(12)
const userTotal = ref(0)

const auditLogs = ref([])

const cameraStats = computed(() => {
  const cameras = cameraStore.cameras || []
  let online = 0
  let offline = 0
  for (const cam of cameras) {
    const status = String(cam.status || 'offline').toLowerCase()
    if (status === 'offline') offline += 1
    else online += 1
  }
  return { total: cameras.length, online, offline }
})

const filteredUsers = computed(() => {
  const query = String(userQuery.value || '').trim().toLowerCase()
  const roleFilter = String(userRoleFilter.value || 'all').toLowerCase()
  return users.value.filter((u) => {
    const roleOk = roleFilter === 'all' || normalizeRole(u.roleRaw || u.role) === roleFilter
    if (!roleOk) return false
    if (!query) return true
    const text = `${u.name || ''} ${u.email || ''} ${u.role || ''}`.toLowerCase()
    return text.includes(query)
  })
})

const userPageCount = computed(() => {
  const total = Math.max(0, Number(userTotal.value) || 0)
  const size = Math.max(1, Number(userPageSize.value) || 12)
  return Math.max(1, Math.ceil(total / size))
})

const systemInfo = computed(() => {
  const h = health.value || {}
  return {
    version: 'v0.1.0',
    build: new Date().toISOString().slice(0, 10),
    node: h.nodeVersion || '--',
    uptime: Number.isFinite(Number(h.uptime)) ? `${Math.floor(Number(h.uptime) / 60)} min` : '--',
    memory: Number.isFinite(Number(h.memoryUsage)) ? `${Math.round(Number(h.memoryUsage) / 1024 / 1024)} MB` : '--',
    status: h.status || 'unknown',
  }
})

onMounted(async () => {
  hydrateProfileFromAuth()
  await Promise.all([
    loadSessions(),
    loadHealth(),
    loadSettings(),
    cameraStore.fetchCameras().catch(() => {}),
  ])
})

function normalizeRole(raw) {
  const value = String(raw || '').trim().toLowerCase()
  if (value === 'admin' || value === 'administrator') return 'admin'
  if (value === 'operator') return 'operator'
  return 'viewer'
}

function roleLabel(raw) {
  const role = normalizeRole(raw)
  if (role === 'admin') return 'Admin'
  if (role === 'operator') return 'Operator'
  return 'Viewer'
}

function hydrateProfileFromAuth() {
  const user = auth.user || {}
  profileForm.value.displayName = user.username || 'Admin User'
  profileForm.value.email = user.email || 'admin@reallive.com'
}

async function loadSettings() {
  loading.value = true
  try {
    const data = await settingsApi.getAll()
    profileForm.value = { ...profileForm.value, ...(data?.profile || {}) }
    notificationCfg.value = { ...notificationCfg.value, ...(data?.notifications || {}) }
    systemCfg.value = { ...systemCfg.value, ...(data?.system || {}) }
    security.value = { ...security.value, ...(data?.security || {}) }
    canManageUsers.value = normalizeRole(profileForm.value.role) === 'admin'
    const rows = Array.isArray(data?.auditLogs) ? data.auditLogs : []
    auditLogs.value = rows.map((row) => ({
      ...row,
      icon: logIcon(row.type),
      time: formatAuditTime(row.time),
    }))
    await loadUsers()
  } catch (err) {
    pushNotice(err?.message || 'Failed to load settings')
  } finally {
    loading.value = false
  }
}

async function loadUsers() {
  if (!canManageUsers.value) {
    users.value = [{
      id: auth.user?.id || 'u1',
      name: profileForm.value.displayName || auth.user?.username || 'Admin',
      email: profileForm.value.email || auth.user?.email || '-',
      role: roleLabel(profileForm.value.role),
      status: 'Online',
      login: 'now',
      avatar: String((profileForm.value.displayName || auth.user?.username || 'A')).slice(0, 2).toUpperCase(),
    }]
    userTotal.value = 1
    userPage.value = 1
    return
  }

  try {
    const size = Math.max(1, Number(userPageSize.value) || 12)
    const page = Math.max(1, Number(userPage.value) || 1)
    const offset = (page - 1) * size
    const data = await settingsApi.listUsers(size, offset)
    const rows = Array.isArray(data?.rows) ? data.rows : []
    userTotal.value = Math.max(rows.length, Number(data?.total || 0))
    if (page > 1 && rows.length === 0) {
      userPage.value = Math.max(1, page - 1)
      await loadUsers()
      return
    }
    const myId = Number(auth.user?.id || 0)
    users.value = rows.map((row) => ({
      id: row.id,
      name: row.username,
      email: row.email,
      role: roleLabel(row.role),
      roleRaw: normalizeRole(row.role),
      status: Number(row.id) === myId ? 'Online' : 'Idle',
      login: Number(row.id) === myId ? 'now' : '-',
      avatar: String(row.username || 'U').slice(0, 2).toUpperCase(),
      createdAt: row.createdAt,
    }))
  } catch (err) {
    pushNotice(err?.message || 'Failed to load users')
  }
}

async function goUserPage(nextPage) {
  const target = Math.max(1, Math.min(Number(nextPage) || 1, userPageCount.value))
  if (target === userPage.value) return
  userPage.value = target
  await loadUsers()
}

async function loadSessions() {
  try {
    const data = await sessionApi.getActive()
    activeSessions.value = Array.isArray(data?.sessions) ? data.sessions : []
  } catch {
    activeSessions.value = []
  }
}

async function loadHealth() {
  try {
    health.value = await dashboardApi.getHealth()
  } catch {
    health.value = null
  }
}

function pushNotice(text) {
  notice.value = text
  setTimeout(() => {
    if (notice.value === text) notice.value = ''
  }, 1800)
}

async function saveProfile() {
  saving.value = true
  try {
    const data = await settingsApi.updateProfile(profileForm.value)
    profileForm.value = { ...profileForm.value, ...(data?.profile || {}) }
    auth.user = {
      ...(auth.user || {}),
      username: profileForm.value.displayName,
      email: profileForm.value.email,
    }
    localStorage.setItem('user', JSON.stringify(auth.user))
    canManageUsers.value = normalizeRole(profileForm.value.role) === 'admin'
    pushNotice('Profile saved')
  } catch (err) {
    pushNotice(err?.message || 'Profile save failed')
  } finally {
    saving.value = false
  }
}

function resetProfile() {
  hydrateProfileFromAuth()
  pushNotice('Profile reset')
}

async function saveNotifications() {
  saving.value = true
  try {
    const data = await settingsApi.updatePreferences({
      notifications: notificationCfg.value,
      system: systemCfg.value,
      security: security.value,
    })
    notificationCfg.value = { ...notificationCfg.value, ...(data?.notifications || {}) }
    systemCfg.value = { ...systemCfg.value, ...(data?.system || {}) }
    security.value = { ...security.value, ...(data?.security || {}) }
    pushNotice('Notification policy saved')
  } catch (err) {
    pushNotice(err?.message || 'Save failed')
  } finally {
    saving.value = false
  }
}

async function saveSystem() {
  saving.value = true
  try {
    const data = await settingsApi.updatePreferences({
      notifications: notificationCfg.value,
      system: systemCfg.value,
      security: security.value,
    })
    notificationCfg.value = { ...notificationCfg.value, ...(data?.notifications || {}) }
    systemCfg.value = { ...systemCfg.value, ...(data?.system || {}) }
    security.value = { ...security.value, ...(data?.security || {}) }
    pushNotice('System settings saved')
  } catch (err) {
    pushNotice(err?.message || 'Save failed')
  } finally {
    saving.value = false
  }
}

function openRevokeSession(session) {
  if (!session?.id) return
  revokeTarget.value = { ...session }
  revokeDrawerOpen.value = true
}

function closeRevokeDrawer() {
  if (saving.value) return
  revokeDrawerOpen.value = false
  revokeTarget.value = null
}

function formatSessionDuration(seconds) {
  const s = Math.max(0, Number(seconds) || 0)
  const h = Math.floor(s / 3600)
  const m = Math.floor((s % 3600) / 60)
  const sec = s % 60
  if (h > 0) return `${h}h ${m}m ${sec}s`
  if (m > 0) return `${m}m ${sec}s`
  return `${sec}s`
}

async function confirmRevokeSession() {
  const sessionId = Number(revokeTarget.value?.id || 0)
  if (!sessionId) return
  saving.value = true
  try {
    await sessionApi.revoke(sessionId)
    await loadSessions()
    pushNotice('Session revoked')
    closeRevokeDrawer()
  } catch (err) {
    pushNotice(err?.message || 'Failed to revoke session')
  } finally {
    saving.value = false
  }
}

async function createUser() {
  if (!canManageUsers.value) {
    pushNotice('Administrator role required')
    return
  }
  openCreateUserModal()
}

async function editUser(user) {
  if (!canManageUsers.value) {
    pushNotice('Administrator role required')
    return
  }
  openEditUserModal(user)
}

async function removeUser(user) {
  if (!canManageUsers.value) {
    pushNotice('Administrator role required')
    return
  }
  const ok = window.confirm(`Delete user ${user.name}?`)
  if (!ok) return
  saving.value = true
  try {
    await settingsApi.deleteUser(user.id)
    pushNotice(`User ${user.name} deleted`)
    await Promise.all([loadUsers(), refreshAudit()])
  } catch (err) {
    pushNotice(err?.message || 'Delete user failed')
  } finally {
    saving.value = false
  }
}

function openCreateUserModal() {
  userModalMode.value = 'create'
  userModalStep.value = 1
  userModalError.value = ''
  userForm.value = {
    id: null,
    username: '',
    email: '',
    role: 'viewer',
    password: '',
  }
  generateTempPassword()
  userModalOpen.value = true
}

function openEditUserModal(user) {
  userModalMode.value = 'edit'
  userModalStep.value = 1
  userModalError.value = ''
  userForm.value = {
    id: user.id,
    username: user.name || '',
    email: user.email || '',
    role: normalizeRole(user.roleRaw || user.role),
    password: '',
  }
  userModalOpen.value = true
}

function closeUserModal() {
  if (saving.value) return
  userModalOpen.value = false
}

function generateTempPassword() {
  const chars = 'ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz23456789!@#$%^&*'
  let out = ''
  for (let i = 0; i < 12; i += 1) {
    const idx = Math.floor(Math.random() * chars.length)
    out += chars[idx]
  }
  userForm.value.password = out
}

async function copyTempPassword() {
  const value = String(userForm.value.password || '')
  if (!value) {
    pushNotice('No password to copy')
    return
  }
  try {
    if (navigator?.clipboard?.writeText) {
      await navigator.clipboard.writeText(value)
      pushNotice('Temporary password copied')
      return
    }
  } catch {
  }
  pushNotice('Clipboard unavailable')
}

function nextUserStep() {
  const username = String(userForm.value.username || '').trim()
  const email = String(userForm.value.email || '').trim().toLowerCase()
  if (!username) {
    userModalError.value = 'Username is required'
    return
  }
  if (!email) {
    userModalError.value = 'Email is required'
    return
  }
  userModalError.value = ''
  userModalStep.value = 2
}

function prevUserStep() {
  userModalError.value = ''
  userModalStep.value = 1
}

async function submitUserModal() {
  const username = String(userForm.value.username || '').trim()
  const email = String(userForm.value.email || '').trim().toLowerCase()
  const role = normalizeRole(userForm.value.role)
  const password = String(userForm.value.password || '')

  if (!username) {
    userModalError.value = 'Username is required'
    return
  }
  if (!email) {
    userModalError.value = 'Email is required'
    return
  }
  if (userModalMode.value === 'create' && userModalStep.value < 2) {
    nextUserStep()
    return
  }
  if (userModalMode.value === 'create' && password.length < 6) {
    userModalError.value = 'Temporary password must be at least 6 characters'
    return
  }

  userModalError.value = ''
  saving.value = true
  try {
    if (userModalMode.value === 'create') {
      await settingsApi.createUser({ username, email, role, password })
      pushNotice(`User ${username} created`)
    } else {
      await settingsApi.updateUser(userForm.value.id, { username, email, role })
      pushNotice(`User ${username} updated`)
    }
    userModalOpen.value = false
    await Promise.all([loadUsers(), refreshAudit()])
  } catch (err) {
    userModalError.value = err?.message || 'Save user failed'
  } finally {
    saving.value = false
  }
}

async function saveSecurityPreferences() {
  saving.value = true
  try {
    const data = await settingsApi.updatePreferences({
      notifications: notificationCfg.value,
      system: systemCfg.value,
      security: security.value,
    })
    security.value = { ...security.value, ...(data?.security || {}) }
    pushNotice('Security preferences saved')
  } catch (err) {
    pushNotice(err?.message || 'Save failed')
  } finally {
    saving.value = false
  }
}

async function changePassword() {
  const currentPassword = String(passwordForm.value.currentPassword || '')
  const newPassword = String(passwordForm.value.newPassword || '')
  const confirmPassword = String(passwordForm.value.confirmPassword || '')
  if (!currentPassword || !newPassword) {
    pushNotice('Current and new password are required')
    return
  }
  if (newPassword.length < 6) {
    pushNotice('New password must be at least 6 characters')
    return
  }
  if (newPassword !== confirmPassword) {
    pushNotice('Confirm password does not match')
    return
  }
  saving.value = true
  try {
    await settingsApi.changePassword(currentPassword, newPassword)
    passwordForm.value.currentPassword = ''
    passwordForm.value.newPassword = ''
    passwordForm.value.confirmPassword = ''
    pushNotice('Password changed')
    await refreshAudit()
  } catch (err) {
    pushNotice(err?.message || 'Password change failed')
  } finally {
    saving.value = false
  }
}

async function refreshAudit() {
  try {
    const data = await settingsApi.getAudit(60)
    const rows = Array.isArray(data?.rows) ? data.rows : []
    auditLogs.value = rows.map((row) => ({
      ...row,
      icon: logIcon(row.type),
      time: formatAuditTime(row.time),
    }))
  } catch {
  }
}

function formatAuditTime(value) {
  if (!value) return '-'
  const dt = new Date(value)
  if (Number.isNaN(dt.getTime())) return String(value)
  return dt.toLocaleString()
}

function logIcon(type) {
  if (type === 'login') return 'login'
  if (type === 'security') return 'shield'
  if (type === 'device') return 'videocam'
  return 'settings'
}

function typeTagClass(type) {
  if (type === 'login') return 'login'
  if (type === 'device') return 'device'
  if (type === 'security') return 'security'
  return 'config'
}

function roleTagClass(role) {
  if (normalizeRole(role) === 'admin') return 'admin'
  if (normalizeRole(role) === 'operator') return 'operator'
  return 'viewer'
}

function userStatusColor(status) {
  if (status === 'Online') return 'var(--green)'
  if (status === 'Idle') return 'var(--ol)'
  return 'var(--red)'
}
</script>

<template>
  <div class="settings-page">
    <div class="settings-layout">
      <div class="settings-tabs" id="settingsTabs">
        <div
          v-for="tab in tabs"
          :key="tab.id"
          class="stab"
          :class="{ active: activeTab === tab.id }"
          @click="activeTab = tab.id"
        >
          <span class="mi">{{ tab.icon }}</span>
          <span class="stab-label">{{ tab.label }}</span>
        </div>
      </div>

      <div>
        <div v-if="notice" class="set-global-notice">{{ notice }}</div>
        <div v-if="loading" class="set-global-notice">Loading settings...</div>

        <div v-if="activeTab === 'profile'" class="set-card">
          <div class="set-card-header">
            <h3><span class="mi">badge</span> Account Profile</h3>
            <span class="set-badge">Synced</span>
          </div>
          <div class="set-card-body">
            <div class="profile-header">
              <div class="profile-avatar">{{ (profileForm.displayName || 'U').slice(0, 2).toUpperCase() }}<div class="edit-badge"><span class="mi">edit</span></div></div>
              <div class="profile-info">
                <h3>{{ profileForm.displayName }}</h3>
                <p>{{ profileForm.email }}</p>
                <span class="profile-role"><span class="mi" style="font-size:14px">admin_panel_settings</span> {{ profileForm.role }}</span>
              </div>
            </div>

            <div class="form-grid">
              <div class="form-field"><label>Display Name</label><input v-model="profileForm.displayName" class="set-input" type="text"></div>
              <div class="form-field"><label>Email</label><input v-model="profileForm.email" class="set-input" type="email"></div>
              <div class="form-field"><label>Phone</label><input v-model="profileForm.phone" class="set-input" type="text"></div>
              <div class="form-field">
                <label>Default Role</label>
                <select v-model="profileForm.role" class="set-select"><option>Administrator</option><option>Operator</option><option>Viewer</option></select>
              </div>
              <div class="form-field full-width"><label>Signature</label><textarea v-model="profileForm.signature" class="set-textarea"></textarea></div>
            </div>

            <div class="set-actions">
              <button class="set-btn" :disabled="saving" @click="resetProfile"><span class="mi">undo</span> Reset</button>
              <button class="set-btn primary" :disabled="saving" @click="saveProfile"><span class="mi">save</span> Save Profile</button>
            </div>
          </div>
        </div>

        <div v-if="activeTab === 'security'" class="set-card">
          <div class="set-card-header">
            <h3><span class="mi">shield_lock</span> Login & Authentication</h3>
          </div>
          <div class="set-card-body">
            <div class="set-row">
              <div class="sr-left"><div class="sr-icon"><span class="mi">password</span></div><div class="sr-text"><h4>Password</h4><p>Use current password to set a new one</p></div></div>
              <div class="sr-right"><button class="set-btn" :disabled="saving" @click="changePassword"><span class="mi">key</span> Update Password</button></div>
            </div>
            <div class="form-grid" style="margin-top:12px">
              <div class="form-field"><label>Current Password</label><input v-model="passwordForm.currentPassword" class="set-input" type="password"></div>
              <div class="form-field"><label>New Password</label><input v-model="passwordForm.newPassword" class="set-input" type="password"></div>
              <div class="form-field"><label>Confirm Password</label><input v-model="passwordForm.confirmPassword" class="set-input" type="password"></div>
            </div>
            <div class="set-row">
              <div class="sr-left"><div class="sr-icon"><span class="mi">phonelink_lock</span></div><div class="sr-text"><h4>Two-Factor Authentication</h4><p>Authenticator app required for administrator login</p></div></div>
              <div class="sr-right"><span class="sr-value">{{ security.twoFactor ? 'Enabled' : 'Disabled' }}</span><button class="sw-web" :class="{ on: security.twoFactor }" @click="security.twoFactor = !security.twoFactor"></button></div>
            </div>
            <div class="set-row">
              <div class="sr-left"><div class="sr-icon"><span class="mi">fingerprint</span></div><div class="sr-text"><h4>Device Trust</h4><p>Keep trusted devices signed in for 14 days</p></div></div>
              <div class="sr-right"><button class="sw-web" :class="{ on: security.trustedDevice }" @click="security.trustedDevice = !security.trustedDevice"></button></div>
            </div>
            <div class="set-row">
              <div class="sr-left"><div class="sr-icon"><span class="mi">vpn_lock</span></div><div class="sr-text"><h4>IP Allowlist</h4><p>Restrict admin logins to office network ranges</p></div></div>
              <div class="sr-right"><button class="sw-web" :class="{ on: security.ipAllowlist }" @click="security.ipAllowlist = !security.ipAllowlist"></button></div>
            </div>
            <div class="set-actions"><button class="set-btn primary" :disabled="saving" @click="saveSecurityPreferences"><span class="mi">save</span> Save Security</button></div>

            <div class="set-card" style="margin-top:16px">
              <div class="set-card-header"><h3><span class="mi">devices</span> Active Sessions</h3></div>
              <div class="set-card-body">
                <div v-if="!activeSessions.length" class="set-empty">No active session.</div>
                <div v-for="session in activeSessions" :key="session.id" class="set-row">
                  <div class="sr-left"><div class="sr-icon"><span class="mi">laptop_mac</span></div><div class="sr-text"><h4>{{ session.device_info || 'Unknown Device' }}</h4><p>{{ session.ip_address || '-' }} · {{ session.user_agent || '-' }}</p></div></div>
                  <div class="sr-right"><button class="set-btn" :disabled="saving" @click="openRevokeSession(session)"><span class="mi">logout</span> Revoke</button></div>
                </div>
              </div>
            </div>
          </div>
        </div>

        <div v-if="activeTab === 'users'" class="set-card">
          <div class="set-card-header">
            <h3><span class="mi">group</span> User Directory</h3>
            <div class="set-actions" style="margin-top:0">
              <button class="set-btn" :disabled="saving" @click="loadUsers"><span class="mi">refresh</span> Refresh</button>
              <button class="set-btn primary" :disabled="saving || !canManageUsers" @click="createUser"><span class="mi">person_add</span> Invite User</button>
            </div>
          </div>
          <div class="set-card-body">
            <div v-if="!canManageUsers" class="set-empty">Only administrators can manage users. You can view your own account here.</div>
            <div class="users-toolbar">
              <input v-model="userQuery" class="set-input" type="text" placeholder="Search users..." />
              <select v-model="userRoleFilter" class="set-select">
                <option value="all">All Roles</option>
                <option value="admin">Admin</option>
                <option value="operator">Operator</option>
                <option value="viewer">Viewer</option>
              </select>
            </div>
            <div class="user-table">
              <div class="ut-row header"><div>User</div><div>Last Login</div><div>Role</div><div>Status</div><div>Actions</div></div>
              <div v-for="user in filteredUsers" :key="user.id" class="ut-row">
                <div class="ut-user"><div class="ut-avatar" style="background:#6D5BD0">{{ user.avatar }}</div><div class="ut-info"><h4>{{ user.name }}</h4><p>{{ user.email }}</p></div></div>
                <div class="ut-cell">{{ user.login }}</div>
                <div class="ut-role"><span class="role-tag" :class="roleTagClass(user.role)">{{ user.role }}</span></div>
                <div class="ut-status"><span class="dot" :style="{ background: userStatusColor(user.status) }"></span> {{ user.status }}</div>
                <div class="ut-actions">
                  <button class="act-btn" :disabled="!canManageUsers || saving" @click="editUser(user)"><span class="mi">edit</span></button>
                  <button class="act-btn danger" :disabled="!canManageUsers || saving" @click="removeUser(user)"><span class="mi">delete</span></button>
                </div>
              </div>
              <div v-if="!filteredUsers.length" class="set-empty">No users match current filter.</div>
            </div>
            <div v-if="canManageUsers" class="users-pagination">
              <span class="up-meta">Total {{ userTotal }} · Page {{ userPage }} / {{ userPageCount }}</span>
              <div class="up-actions">
                <button class="set-btn" :disabled="saving || userPage <= 1" @click="goUserPage(userPage - 1)">Prev</button>
                <button class="set-btn" :disabled="saving || userPage >= userPageCount" @click="goUserPage(userPage + 1)">Next</button>
              </div>
            </div>
          </div>
        </div>

        <div v-if="activeTab === 'audit'" class="set-card">
          <div class="set-card-header"><h3><span class="mi">history</span> Operation Audit Log</h3><button class="set-btn" @click="refreshAudit"><span class="mi">refresh</span> Refresh</button></div>
          <div class="set-card-body">
            <div class="audit-row header"><div>Time</div><div>Action</div><div>User</div><div>Type</div></div>
            <div v-if="!auditLogs.length" class="set-empty">No audit logs yet.</div>
            <div v-for="row in auditLogs" :key="row.id" class="audit-row">
              <div class="au-time">{{ row.time }}</div>
              <div class="au-action"><span class="mi">{{ row.icon }}</span><span>{{ row.action }}</span></div>
              <div class="au-user">{{ row.user }}</div>
              <div class="au-type"><span class="type-tag" :class="typeTagClass(row.type)">{{ row.type }}</span></div>
            </div>
          </div>
        </div>

        <div v-if="activeTab === 'notifications'" class="set-card">
          <div class="set-card-header"><h3><span class="mi">campaign</span> Notification Channels</h3></div>
          <div class="set-card-body">
            <div class="notif-channel"><div class="nc-icon"><span class="mi">email</span></div><div class="nc-info"><h4>Email Alerts</h4><p>Send high-severity alert summary to security distribution list</p></div><div class="nc-right"><span class="nc-status" :class="{ off: !notificationCfg.email }">{{ notificationCfg.email ? 'Enabled' : 'Disabled' }}</span><button class="sw-web" :class="{ on: notificationCfg.email }" @click="notificationCfg.email = !notificationCfg.email"></button></div></div>
            <div class="notif-channel"><div class="nc-icon"><span class="mi">sms</span></div><div class="nc-info"><h4>SMS Alerts</h4><p>Use for critical incidents only</p></div><div class="nc-right"><span class="nc-status" :class="{ off: !notificationCfg.sms }">{{ notificationCfg.sms ? 'Enabled' : 'Disabled' }}</span><button class="sw-web" :class="{ on: notificationCfg.sms }" @click="notificationCfg.sms = !notificationCfg.sms"></button></div></div>
            <div class="notif-channel"><div class="nc-icon"><span class="mi">chat</span></div><div class="nc-info"><h4>Webhook (Slack/Teams)</h4><p>Push structured events to collaboration channels</p></div><div class="nc-right"><span class="nc-status" :class="{ off: !notificationCfg.webhook }">{{ notificationCfg.webhook ? 'Enabled' : 'Disabled' }}</span><button class="sw-web" :class="{ on: notificationCfg.webhook }" @click="notificationCfg.webhook = !notificationCfg.webhook"></button></div></div>

            <div class="form-grid" style="margin-top:14px">
              <div class="form-field"><label>Quiet Hours</label><select v-model="notificationCfg.quietHours" class="set-select"><option>22:00 - 06:00</option><option>23:00 - 07:00</option><option>Disabled</option></select></div>
              <div class="form-field"><label>Escalation Delay</label><select v-model="notificationCfg.escalationDelay" class="set-select"><option>Immediately</option><option>After 60 seconds</option><option>After 180 seconds</option></select></div>
              <div class="form-field full-width"><label>Escalation Rule</label><textarea v-model="notificationCfg.escalationRule" class="set-textarea"></textarea></div>
            </div>
            <div class="set-actions"><button class="set-btn primary" :disabled="saving" @click="saveNotifications"><span class="mi">save</span> Save Policy</button></div>
          </div>
        </div>

        <div v-if="activeTab === 'system'" class="set-card">
          <div class="set-card-header"><h3><span class="mi">dns</span> Runtime Configuration</h3></div>
          <div class="set-card-body">
            <div class="set-row"><div class="sr-left"><div class="sr-icon"><span class="mi">router</span></div><div class="sr-text"><h4>NVR Mode</h4><p>Select recording mode for all devices in current tenant</p></div></div><div class="sr-right"><select v-model="systemCfg.nvrMode" class="set-select" style="width:190px"><option>Continuous + Event</option><option>Event Priority</option><option>Manual Only</option></select></div></div>
            <div class="set-row"><div class="sr-left"><div class="sr-icon"><span class="mi">sync</span></div><div class="sr-text"><h4>Time Synchronization</h4><p>Sync all cameras with central NTP to keep playback alignment</p></div></div><div class="sr-right"><button class="set-btn" @click="pushNotice('Manual NTP sync triggered')"><span class="mi">sync</span> Sync Now</button></div></div>
            <div class="set-row"><div class="sr-left"><div class="sr-icon"><span class="mi">network_check</span></div><div class="sr-text"><h4>Network Health Probe</h4><p>Continuously test uplink quality and packet loss</p></div></div><div class="sr-right"><button class="sw-web" :class="{ on: systemCfg.networkProbe }" @click="systemCfg.networkProbe = !systemCfg.networkProbe"></button></div></div>
          </div>
        </div>

        <div v-if="activeTab === 'system'" class="set-card" style="margin-top:16px">
          <div class="set-card-header"><h3><span class="mi">info</span> About This Tenant</h3></div>
          <div class="set-card-body">
            <div class="about-grid">
              <div class="about-item"><div class="ai-label">Console Version</div><div class="ai-value">{{ systemInfo.version }}</div></div>
              <div class="about-item"><div class="ai-label">Backend Status</div><div class="ai-value">{{ systemInfo.status }}</div></div>
              <div class="about-item"><div class="ai-label">Node.js</div><div class="ai-value">{{ systemInfo.node }}</div></div>
              <div class="about-item"><div class="ai-label">Backend Uptime</div><div class="ai-value">{{ systemInfo.uptime }}</div></div>
              <div class="about-item"><div class="ai-label">Memory</div><div class="ai-value">{{ systemInfo.memory }}</div></div>
              <div class="about-item"><div class="ai-label">Cameras</div><div class="ai-value">{{ cameraStats.total }} total · {{ cameraStats.online }} online · {{ cameraStats.offline }} offline</div></div>
            </div>
            <div class="set-actions"><button class="set-btn" :disabled="saving" @click="saveSystem"><span class="mi">save</span> Save Changes</button></div>
          </div>
        </div>
      </div>
    </div>

    <div v-if="userModalOpen" class="um-overlay" @click.self="closeUserModal">
      <div class="um-drawer">
        <div class="um-header">
          <h3>{{ userModalMode === 'create' ? 'Invite User' : 'Edit User' }}</h3>
          <button class="um-close" :disabled="saving" @click="closeUserModal"><span class="mi">close</span></button>
        </div>
        <div class="um-body">
          <div v-if="userModalMode === 'create'" class="um-step">
            <span class="tag" :class="{ active: userModalStep === 1 }">1. Profile</span>
            <span class="tag" :class="{ active: userModalStep === 2 }">2. Access</span>
          </div>

          <div v-if="userModalMode === 'edit' || userModalStep === 1" class="form-grid">
            <div class="form-field">
              <label>Username</label>
              <input v-model="userForm.username" class="set-input" type="text" />
            </div>
            <div class="form-field">
              <label>Email</label>
              <input v-model="userForm.email" class="set-input" type="email" />
            </div>
            <div class="form-field">
              <label>Role</label>
              <select v-model="userForm.role" class="set-select">
                <option value="admin">Admin</option>
                <option value="operator">Operator</option>
                <option value="viewer">Viewer</option>
              </select>
            </div>
          </div>

          <div v-if="userModalMode === 'create' && userModalStep === 2" class="form-grid">
            <div class="form-field full-width">
              <label>Temporary Password</label>
              <div class="pw-row">
                <input v-model="userForm.password" class="set-input" type="text" />
                <button class="set-btn" :disabled="saving" @click="generateTempPassword"><span class="mi">auto_fix_high</span>Generate</button>
                <button class="set-btn" :disabled="saving" @click="copyTempPassword"><span class="mi">content_copy</span>Copy</button>
              </div>
            </div>
          </div>
          <div v-if="userModalError" class="um-error">{{ userModalError }}</div>
        </div>
        <div class="um-actions">
          <button v-if="userModalMode === 'create' && userModalStep === 2" class="set-btn" :disabled="saving" @click="prevUserStep">
            <span class="mi">arrow_back</span>Back
          </button>
          <button class="set-btn" :disabled="saving" @click="closeUserModal">Cancel</button>
          <button
            v-if="userModalMode === 'create' && userModalStep === 1"
            class="set-btn primary"
            :disabled="saving"
            @click="nextUserStep"
          >
            Next<span class="mi">arrow_forward</span>
          </button>
          <button
            v-else
            class="set-btn primary"
            :disabled="saving"
            @click="submitUserModal"
          >
            {{ userModalMode === 'create' ? 'Create User' : 'Save Changes' }}
          </button>
        </div>
      </div>
    </div>

    <div v-if="revokeDrawerOpen" class="um-overlay" @click.self="closeRevokeDrawer">
      <div class="um-drawer">
        <div class="um-header">
          <h3>Revoke Session</h3>
          <button class="um-close" :disabled="saving" @click="closeRevokeDrawer"><span class="mi">close</span></button>
        </div>
        <div class="um-body">
          <div class="about-grid">
            <div class="about-item">
              <div class="ai-label">Device</div>
              <div class="ai-value">{{ revokeTarget?.device_info || 'Unknown Device' }}</div>
            </div>
            <div class="about-item">
              <div class="ai-label">IP Address</div>
              <div class="ai-value">{{ revokeTarget?.ip_address || '-' }}</div>
            </div>
            <div class="about-item">
              <div class="ai-label">Duration</div>
              <div class="ai-value">{{ formatSessionDuration(revokeTarget?.duration_seconds) }}</div>
            </div>
            <div class="about-item">
              <div class="ai-label">Started At</div>
              <div class="ai-value">{{ revokeTarget?.start_time || '-' }}</div>
            </div>
          </div>
          <div class="um-error" style="margin-top:14px">
            This will immediately end this active login session.
          </div>
        </div>
        <div class="um-actions">
          <button class="set-btn" :disabled="saving" @click="closeRevokeDrawer">Cancel</button>
          <button class="set-btn primary" :disabled="saving" @click="confirmRevokeSession">
            <span class="mi">logout</span>Confirm Revoke
          </button>
        </div>
      </div>
    </div>
  </div>
</template>

<style scoped>
.settings-page {
  height: 100%;
}

.settings-layout {
  display: grid;
  grid-template-columns: 248px 1fr;
  gap: 20px;
}

.settings-tabs {
  background: var(--sc);
  border-radius: var(--r3);
  padding: 10px;
  align-self: flex-start;
  position: sticky;
  top: 0;
}

.stab {
  display: flex;
  align-items: center;
  gap: 10px;
  padding: 11px 12px;
  border-radius: var(--r2);
  cursor: pointer;
  transition: background .15s;
  margin-bottom: 4px;
  min-height: 44px;
}

.stab:hover {
  background: var(--sc2);
}

.stab.active {
  background: var(--pri-c);
  color: var(--on-pri-c);
}

.stab .mi {
  font-size: 20px;
  color: var(--on-sfv);
  flex-shrink: 0;
  width: 20px;
  text-align: center;
}

.stab.active .mi {
  color: var(--on-pri-c);
}

.stab .stab-label {
  font: 500 13px/18px 'Roboto', sans-serif;
  white-space: nowrap;
}

.set-global-notice {
  margin-bottom: 12px;
  padding: 10px 12px;
  border: 1px solid rgba(125,216,129,.35);
  background: rgba(125,216,129,.12);
  color: #86efac;
  border-radius: var(--r2);
  font: 500 12px/16px 'Roboto', sans-serif;
}

.set-card {
  background: var(--sc);
  border-radius: var(--r3);
  overflow: hidden;
}

.set-card-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 18px 20px;
  border-bottom: 1px solid var(--olv);
}

.set-card-header h3 {
  font: 500 16px/24px 'Roboto', sans-serif;
  display: flex;
  align-items: center;
  gap: 8px;
}

.set-card-header h3 .mi {
  font-size: 20px;
  color: var(--pri);
}

.set-badge {
  padding: 2px 10px;
  border-radius: var(--r6);
  font: 500 11px/16px 'Roboto', sans-serif;
  color: var(--green);
  background: rgba(125,216,129,.12);
}

.set-card-body {
  padding: 20px;
}

.profile-header {
  display: flex;
  align-items: center;
  gap: 20px;
  margin-bottom: 24px;
}

.profile-avatar {
  width: 80px;
  height: 80px;
  border-radius: 50%;
  background: linear-gradient(135deg, var(--pri), var(--ter));
  display: flex;
  align-items: center;
  justify-content: center;
  font: 500 28px/36px 'Roboto', sans-serif;
  color: #fff;
  position: relative;
  flex-shrink: 0;
}

.profile-avatar .edit-badge {
  position: absolute;
  bottom: 0;
  right: 0;
  width: 26px;
  height: 26px;
  border-radius: 50%;
  background: var(--pri);
  display: flex;
  align-items: center;
  justify-content: center;
  border: 3px solid var(--sc);
}

.profile-avatar .edit-badge .mi {
  font-size: 14px;
  color: var(--on-pri);
}

.profile-info h3 {
  font: 500 20px/28px 'Roboto', sans-serif;
  margin-bottom: 2px;
}

.profile-info p {
  font: 400 13px/18px 'Roboto', sans-serif;
  color: var(--on-sfv);
}

.profile-info .profile-role {
  display: inline-flex;
  align-items: center;
  gap: 4px;
  padding: 2px 10px;
  border-radius: var(--r6);
  background: rgba(200,191,255,.12);
  color: var(--pri);
  font: 500 12px/18px 'Roboto', sans-serif;
  margin-top: 4px;
}

.form-grid {
  display: grid;
  grid-template-columns: 1fr 1fr;
  gap: 16px;
}

.form-field.full-width {
  grid-column: 1 / -1;
}

.form-field label {
  display: block;
  margin-bottom: 6px;
  font: 500 12px/16px 'Roboto', sans-serif;
  color: var(--on-sfv);
}

.set-input, .set-select, .set-textarea {
  width: 100%;
  border: 1px solid var(--olv);
  border-radius: var(--r2);
  background: var(--sc2);
  color: var(--on-sf);
  font: 400 13px/18px 'Roboto', sans-serif;
  outline: none;
  padding: 0 12px;
}

.set-input, .set-select {
  height: 38px;
}

.set-textarea {
  min-height: 84px;
  padding: 10px 12px;
  resize: vertical;
}

.set-actions {
  display: flex;
  justify-content: flex-end;
  gap: 8px;
  margin-top: 16px;
}

.set-btn {
  height: 34px;
  padding: 0 14px;
  border-radius: var(--r6);
  border: 1px solid var(--olv);
  background: transparent;
  color: var(--on-sfv);
  font: 500 12px/16px 'Roboto', sans-serif;
  cursor: pointer;
  display: flex;
  align-items: center;
  gap: 5px;
}

.set-btn.primary {
  background: var(--pri);
  color: var(--on-pri);
  border-color: var(--pri);
}

.set-btn .mi {
  font-size: 16px;
}

.set-row {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 14px 0;
  border-bottom: 1px solid var(--olv);
  gap: 16px;
}

.set-row:last-child {
  border-bottom: none;
}

.sr-left {
  display: flex;
  align-items: center;
  gap: 14px;
  flex: 1;
  min-width: 0;
}

.sr-right {
  display: inline-flex;
  align-items: center;
  gap: 8px;
}

.sr-value {
  font: 500 12px/16px 'Roboto', sans-serif;
  color: var(--on-sfv);
}

.sr-icon {
  width: 38px;
  height: 38px;
  border-radius: var(--r2);
  display: flex;
  align-items: center;
  justify-content: center;
  background: var(--sc2);
}

.sr-icon .mi {
  font-size: 20px;
  color: var(--on-sfv);
}

.sr-text h4 {
  font: 500 14px/20px 'Roboto', sans-serif;
}

.sr-text p {
  font: 400 12px/16px 'Roboto', sans-serif;
  color: var(--on-sfv);
}

.sw-web {
  width: 42px;
  height: 24px;
  border-radius: 12px;
  background: var(--sfv);
  position: relative;
  cursor: pointer;
  border: none;
}

.sw-web.on {
  background: var(--pri);
}

.sw-web::after {
  content: '';
  position: absolute;
  top: 3px;
  left: 3px;
  width: 18px;
  height: 18px;
  border-radius: 50%;
  background: var(--ol);
}

.sw-web.on::after {
  left: 21px;
  background: var(--on-pri);
}

.set-empty {
  color: var(--on-sfv);
  font: 400 12px/16px 'Roboto', sans-serif;
}

.users-toolbar {
  display: grid;
  grid-template-columns: 1fr 180px;
  gap: 10px;
  margin-bottom: 12px;
}

.users-pagination {
  margin-top: 12px;
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 12px;
}

.users-pagination .up-meta {
  color: var(--on-sfv);
  font: 400 12px/16px 'Roboto', sans-serif;
}

.users-pagination .up-actions {
  display: flex;
  align-items: center;
  gap: 8px;
}

.user-table .ut-row {
  display: grid;
  grid-template-columns: 1fr 160px 120px 100px 80px;
  align-items: center;
  padding: 12px 0;
  border-bottom: 1px solid var(--olv);
  gap: 12px;
}

.user-table .ut-row.header {
  font: 500 12px/16px 'Roboto', sans-serif;
  color: var(--on-sfv);
  text-transform: uppercase;
  letter-spacing: .5px;
}

.ut-user {
  display: flex;
  align-items: center;
  gap: 10px;
}

.ut-avatar {
  width: 32px;
  height: 32px;
  border-radius: 50%;
  display: flex;
  align-items: center;
  justify-content: center;
  font: 500 12px/16px 'Roboto', sans-serif;
  color: #fff;
}

.ut-info h4 {
  font: 500 14px/20px 'Roboto', sans-serif;
}

.ut-info p, .ut-cell {
  font: 400 12px/16px 'Roboto', sans-serif;
  color: var(--on-sfv);
}

.role-tag {
  display: inline-flex;
  padding: 3px 10px;
  border-radius: var(--r6);
  font: 500 11px/16px 'Roboto', sans-serif;
}

.role-tag.admin { background: rgba(200,191,255,.15); color: var(--pri); }
.role-tag.operator { background: rgba(255,158,67,.12); color: var(--orange); }
.role-tag.viewer { background: rgba(125,216,129,.12); color: var(--green); }

.ut-status {
  display: flex;
  align-items: center;
  gap: 5px;
  font: 500 12px/16px 'Roboto', sans-serif;
}

.ut-status .dot {
  width: 6px;
  height: 6px;
  border-radius: 50%;
}

.ut-actions .act-btn {
  width: 28px;
  height: 28px;
  border-radius: 50%;
  border: none;
  background: transparent;
  cursor: pointer;
}

.ut-actions .act-btn .mi {
  font-size: 18px;
  color: var(--on-sfv);
}

.ut-actions {
  display: flex;
  align-items: center;
  gap: 4px;
}

.ut-actions .act-btn.danger .mi {
  color: #ff7a7a;
}

.ut-actions .act-btn:disabled {
  opacity: .45;
  cursor: not-allowed;
}

.audit-row {
  display: grid;
  grid-template-columns: 180px 1fr 140px 100px;
  align-items: center;
  padding: 10px 0;
  border-bottom: 1px solid var(--olv);
  gap: 12px;
  font: 400 13px/18px 'Roboto', sans-serif;
}

.audit-row.header {
  font: 500 12px/16px 'Roboto', sans-serif;
  color: var(--on-sfv);
  text-transform: uppercase;
}

.au-time { color: var(--ol); }

.au-action {
  display: flex;
  align-items: center;
  gap: 8px;
}

.au-action .mi {
  font-size: 16px;
  color: var(--on-sfv);
}

.type-tag {
  display: inline-flex;
  padding: 2px 8px;
  border-radius: var(--r6);
  font: 500 11px/16px 'Roboto', sans-serif;
}

.type-tag.login { background: rgba(200,191,255,.12); color: var(--pri); }
.type-tag.config { background: rgba(255,158,67,.12); color: var(--orange); }
.type-tag.device { background: rgba(125,216,129,.12); color: var(--green); }
.type-tag.security { background: rgba(255,107,107,.12); color: var(--red); }

.notif-channel {
  display: flex;
  align-items: center;
  gap: 14px;
  padding: 14px 0;
  border-bottom: 1px solid var(--olv);
}

.notif-channel .nc-icon {
  width: 40px;
  height: 40px;
  border-radius: var(--r2);
  display: flex;
  align-items: center;
  justify-content: center;
  background: var(--sc2);
}

.notif-channel .nc-icon .mi {
  font-size: 20px;
  color: var(--on-sfv);
}

.notif-channel .nc-info {
  flex: 1;
}

.notif-channel .nc-info h4 {
  font: 500 14px/20px 'Roboto', sans-serif;
}

.notif-channel .nc-info p {
  font: 400 12px/16px 'Roboto', sans-serif;
  color: var(--on-sfv);
}

.nc-right {
  display: inline-flex;
  align-items: center;
  gap: 8px;
}

.nc-status {
  font: 500 12px/16px 'Roboto', sans-serif;
  color: var(--green);
}

.nc-status.off {
  color: var(--ol);
}

.about-grid {
  display: grid;
  grid-template-columns: 1fr 1fr;
  gap: 14px;
}

.about-item {
  background: var(--sc2);
  border-radius: var(--r2);
  padding: 14px 16px;
}

.ai-label {
  font: 400 12px/16px 'Roboto', sans-serif;
  color: var(--on-sfv);
  margin-bottom: 4px;
}

.ai-value {
  font: 500 14px/20px 'Roboto', sans-serif;
}

.um-overlay {
  position: fixed;
  inset: 0;
  background: rgba(4, 6, 20, .62);
  z-index: 60;
  display: flex;
  justify-content: flex-end;
}

.um-drawer {
  width: min(460px, 100%);
  height: 100%;
  background: var(--sc);
  border-left: 1px solid var(--olv);
  border-radius: 0;
  overflow: auto;
  box-shadow: -16px 0 36px rgba(0, 0, 0, .35);
}

.um-header {
  padding: 14px 16px;
  border-bottom: 1px solid var(--olv);
  display: flex;
  align-items: center;
  justify-content: space-between;
}

.um-header h3 {
  font: 500 15px/22px 'Roboto', sans-serif;
}

.um-close {
  border: none;
  background: transparent;
  color: var(--on-sfv);
  cursor: pointer;
  width: 30px;
  height: 30px;
  border-radius: 50%;
  display: flex;
  align-items: center;
  justify-content: center;
}

.um-close .mi {
  font-size: 18px;
}

.um-body {
  padding: 16px;
}

.um-step {
  display: flex;
  gap: 8px;
  margin-bottom: 12px;
}

.um-step .tag {
  border: 1px solid var(--olv);
  color: var(--on-sfv);
  background: var(--sc2);
  border-radius: var(--r6);
  padding: 4px 10px;
  font: 500 11px/16px 'Roboto', sans-serif;
}

.um-step .tag.active {
  color: var(--on-pri);
  background: var(--pri);
  border-color: var(--pri);
}

.pw-row {
  display: grid;
  grid-template-columns: 1fr auto auto;
  gap: 8px;
  align-items: center;
}

.um-error {
  margin-top: 12px;
  border: 1px solid rgba(255, 122, 122, .5);
  background: rgba(255, 122, 122, .15);
  color: #ffb7b7;
  border-radius: var(--r2);
  padding: 8px 10px;
  font: 500 12px/16px 'Roboto', sans-serif;
}

.um-actions {
  padding: 12px 16px 16px;
  border-top: 1px solid var(--olv);
  display: flex;
  justify-content: flex-end;
  gap: 8px;
}

@media (max-width: 1023px) {
  .settings-layout {
    grid-template-columns: 1fr;
  }

  .settings-tabs {
    position: static;
    display: grid;
    grid-template-columns: repeat(3, 1fr);
    gap: 6px;
  }

  .stab {
    justify-content: center;
    padding: 10px;
    min-height: 40px;
  }

  .stab .stab-label {
    font-size: 12px;
  }

  .form-grid,
  .about-grid {
    grid-template-columns: 1fr;
  }

  .users-toolbar {
    grid-template-columns: 1fr;
  }

  .user-table .ut-row {
    grid-template-columns: 1fr 120px 90px 80px;
  }

  .user-table .ut-row > :nth-child(2) {
    display: none;
  }

  .audit-row {
    grid-template-columns: 1fr 110px 90px;
  }

  .audit-row > :nth-child(2) {
    display: none;
  }

  .um-drawer {
    width: 100%;
  }

  .pw-row {
    grid-template-columns: 1fr;
  }
}
</style>
