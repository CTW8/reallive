<script setup>
import { ref } from 'vue'

const activeTab = ref('profile')

const tabs = [
  { id: 'profile', label: 'Profile', icon: 'person' },
  { id: 'security', label: 'Security', icon: 'security' },
  { id: 'users', label: 'Users & Roles', icon: 'groups' },
  { id: 'audit', label: 'Audit Logs', icon: 'history' },
  { id: 'notifications', label: 'Notifications', icon: 'notifications' },
  { id: 'system', label: 'System', icon: 'settings_applications' },
]
</script>

<template>
  <div class="settings-page">
    <div class="settings-layout">
      <div class="settings-tabs">
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

      <div class="settings-panel active">
        <div v-if="activeTab === 'profile'" class="set-card">
          <div class="set-card-header">
            <h3><span class="mi">person</span> Profile</h3>
          </div>
          <div class="set-card-body">
            <div class="profile-header">
              <div class="profile-avatar">
                L
                <div class="edit-badge"><span class="mi">edit</span></div>
              </div>
              <div class="profile-info">
                <h3>Admin User</h3>
                <p>admin@reallive.com</p>
                <span class="profile-role"><span class="mi">admin_panel_settings</span> Administrator</span>
              </div>
            </div>
            <div class="form-grid">
              <div class="form-field">
                <label>Display Name</label>
                <input type="text" class="set-input" value="Admin User">
              </div>
              <div class="form-field">
                <label>Email</label>
                <input type="email" class="set-input" value="admin@reallive.com">
              </div>
              <div class="form-field">
                <label>Language</label>
                <select class="set-select">
                  <option selected>English</option>
                  <option>中文</option>
                </select>
              </div>
              <div class="form-field">
                <label>Timezone</label>
                <select class="set-select">
                  <option selected>UTC+8 (Asia/Shanghai)</option>
                  <option>UTC+0 (London)</option>
                </select>
              </div>
            </div>
            <div class="set-actions">
              <button class="set-btn primary"><span class="mi">save</span> Save Changes</button>
            </div>
          </div>
        </div>

        <div v-if="activeTab === 'security'" class="set-card">
          <div class="set-card-header">
            <h3><span class="mi">security</span> Security Settings</h3>
          </div>
          <div class="set-card-body">
            <div class="set-row">
              <div class="sr-left">
                <div class="sr-icon"><span class="mi">lock</span></div>
                <div class="sr-text">
                  <h4>Password</h4>
                  <p>Last changed 30 days ago</p>
                </div>
              </div>
              <button class="set-btn">Change</button>
            </div>
            <div class="set-row">
              <div class="sr-left">
                <div class="sr-icon"><span class="mi">verified_user</span></div>
                <div class="sr-text">
                  <h4>Two-Factor Authentication</h4>
                  <p>Add extra security to your account</p>
                </div>
              </div>
              <div class="sw-web on"></div>
            </div>
            <div class="set-row">
              <div class="sr-left">
                <div class="sr-icon"><span class="mi">devices</span></div>
                <div class="sr-text">
                  <h4>Active Sessions</h4>
                  <p>2 devices currently logged in</p>
                </div>
              </div>
              <button class="set-btn">Manage</button>
            </div>
          </div>
        </div>

        <div v-if="activeTab === 'users'" class="set-card">
          <div class="set-card-header">
            <h3><span class="mi">groups</span> Users & Roles</h3>
            <button class="set-btn primary"><span class="mi">person_add</span> Add User</button>
          </div>
          <div class="set-card-body">
            <div class="user-table">
              <div class="ut-row header">
                <div>User</div>
                <div>Email</div>
                <div>Role</div>
                <div>Status</div>
                <div></div>
              </div>
              <div class="ut-row">
                <div class="ut-user">
                  <div class="ut-avatar" style="background:linear-gradient(135deg,var(--pri),var(--ter))">A</div>
                  <div class="ut-info">
                    <h4>Admin User</h4>
                    <p>admin@reallive.com</p>
                  </div>
                </div>
                <div class="ut-cell">admin@reallive.com</div>
                <div class="ut-role"><span class="role-tag admin">Admin</span></div>
                <div class="ut-status"><span class="dot" style="background:var(--green)"></span> Active</div>
                <div class="ut-actions">
                  <button class="act-btn"><span class="mi">edit</span></button>
                </div>
              </div>
              <div class="ut-row">
                <div class="ut-user">
                  <div class="ut-avatar" style="background:linear-gradient(135deg,var(--orange),var(--ter))">O</div>
                  <div class="ut-info">
                    <h4>Operator 1</h4>
                    <p>operator@reallive.com</p>
                  </div>
                </div>
                <div class="ut-cell">operator@reallive.com</div>
                <div class="ut-role"><span class="role-tag operator">Operator</span></div>
                <div class="ut-status"><span class="dot" style="background:var(--green)"></span> Active</div>
                <div class="ut-actions">
                  <button class="act-btn"><span class="mi">edit</span></button>
                  <button class="act-btn"><span class="mi">delete</span></button>
                </div>
              </div>
            </div>
          </div>
        </div>

        <div v-if="activeTab === 'audit'" class="set-card">
          <div class="set-card-header">
            <h3><span class="mi">history</span> Audit Logs</h3>
          </div>
          <div class="set-card-body">
            <div class="audit-row header">
              <div>Time</div>
              <div>Action</div>
              <div>User</div>
              <div>Type</div>
            </div>
            <div class="audit-row">
              <div class="au-time">2026-02-18 14:23</div>
              <div class="au-action"><span class="mi">login</span> <span>User logged in</span></div>
              <div class="au-user">admin@reallive.com</div>
              <div class="au-type"><span class="type-tag login">Login</span></div>
            </div>
            <div class="audit-row">
              <div class="au-time">2026-02-18 14:20</div>
              <div class="au-action"><span class="mi">videocam</span> <span>Camera settings updated</span></div>
              <div class="au-user">operator@reallive.com</div>
              <div class="au-type"><span class="type-tag device">Device</span></div>
            </div>
            <div class="audit-row">
              <div class="au-time">2026-02-18 13:45</div>
              <div class="au-action"><span class="mi">settings</span> <span>System configuration changed</span></div>
              <div class="au-user">admin@reallive.com</div>
              <div class="au-type"><span class="type-tag config">Config</span></div>
            </div>
          </div>
        </div>

        <div v-if="activeTab === 'notifications'" class="set-card">
          <div class="set-card-header">
            <h3><span class="mi">notifications</span> Notification Channels</h3>
          </div>
          <div class="set-card-body">
            <div class="notif-channel">
              <div class="nc-icon"><span class="mi">email</span></div>
              <div class="nc-info">
                <h4>Email Notifications</h4>
                <p>Receive alerts via email</p>
              </div>
              <div class="nc-status">Enabled</div>
              <div class="sw-web on"></div>
            </div>
            <div class="notif-channel">
              <div class="nc-icon"><span class="mi">smartphone</span></div>
              <div class="nc-info">
                <h4>Push Notifications</h4>
                <p>Mobile push alerts</p>
              </div>
              <div class="nc-status off">Disabled</div>
              <div class="sw-web"></div>
            </div>
            <div class="notif-channel">
              <div class="nc-icon"><span class="mi">webhook</span></div>
              <div class="nc-info">
                <h4>Webhook</h4>
                <p>Send events to external services</p>
              </div>
              <div class="nc-status off">Disabled</div>
              <div class="sw-web"></div>
            </div>
          </div>
        </div>

        <div v-if="activeTab === 'system'" class="set-card">
          <div class="set-card-header">
            <h3><span class="mi">settings_applications</span> System Information</h3>
          </div>
          <div class="set-card-body">
            <div class="about-grid">
              <div class="about-item">
                <div class="ai-label">Version</div>
                <div class="ai-value">v0.1.0</div>
              </div>
              <div class="about-item">
                <div class="ai-label">Build</div>
                <div class="ai-value">2026-02-18</div>
              </div>
              <div class="about-item">
                <div class="ai-label">Node.js</div>
                <div class="ai-value">v20.10.0</div>
              </div>
              <div class="about-item">
                <div class="ai-label">Database</div>
                <div class="ai-value">SQLite 3.45</div>
              </div>
            </div>
            <div class="set-actions" style="margin-top:20px">
              <button class="set-btn"><span class="mi">refresh</span> Check Updates</button>
              <button class="set-btn"><span class="mi">description</span> View Logs</button>
            </div>
          </div>
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

.settings-panel {
  display: none;
}

.settings-panel.active {
  display: block;
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
  cursor: pointer;
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

.form-grid .form-field {
  margin-bottom: 0;
}

.set-input, .set-select {
  width: 100%;
  border: 1px solid var(--olv);
  border-radius: var(--r2);
  background: var(--sc2);
  color: var(--on-sf);
  font: 400 13px/18px 'Roboto', sans-serif;
  outline: none;
  height: 38px;
  padding: 0 12px;
}

.set-input:focus, .set-select:focus {
  border-color: var(--pri);
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
  transition: all .15s;
}

.set-btn:hover {
  background: var(--sc2);
}

.set-btn .mi {
  font-size: 16px;
}

.set-btn.primary {
  background: var(--pri);
  color: var(--on-pri);
  border-color: var(--pri);
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

.set-row .sr-left {
  display: flex;
  align-items: center;
  gap: 14px;
  flex: 1;
  min-width: 0;
}

.set-row .sr-icon {
  width: 38px;
  height: 38px;
  border-radius: var(--r2);
  display: flex;
  align-items: center;
  justify-content: center;
  flex-shrink: 0;
  background: var(--sc2);
}

.set-row .sr-icon .mi {
  font-size: 20px;
  color: var(--on-sfv);
}

.set-row .sr-text {
  min-width: 0;
}

.set-row .sr-text h4 {
  font: 500 14px/20px 'Roboto', sans-serif;
}

.set-row .sr-text p {
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
  flex-shrink: 0;
  transition: background .2s;
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
  transition: all .2s;
}

.sw-web.on::after {
  left: 21px;
  background: var(--on-pri);
}

.user-table {
  width: 100%;
}

.user-table .ut-row {
  display: grid;
  grid-template-columns: 1fr 160px 120px 100px 80px;
  align-items: center;
  padding: 12px 0;
  border-bottom: 1px solid var(--olv);
  gap: 12px;
}

.user-table .ut-row:last-child {
  border-bottom: none;
}

.user-table .ut-row.header {
  font: 500 12px/16px 'Roboto', sans-serif;
  color: var(--on-sfv);
  text-transform: uppercase;
  letter-spacing: .5px;
}

.ut-row .ut-user {
  display: flex;
  align-items: center;
  gap: 10px;
  min-width: 0;
}

.ut-row .ut-avatar {
  width: 32px;
  height: 32px;
  border-radius: 50%;
  display: flex;
  align-items: center;
  justify-content: center;
  font: 500 12px/16px 'Roboto', sans-serif;
  color: #fff;
  flex-shrink: 0;
}

.ut-row .ut-info {
  min-width: 0;
}

.ut-row .ut-info h4 {
  font: 500 14px/20px 'Roboto', sans-serif;
}

.ut-row .ut-info p {
  font: 400 12px/16px 'Roboto', sans-serif;
  color: var(--on-sfv);
}

.ut-row .ut-cell {
  font: 400 13px/18px 'Roboto', sans-serif;
  color: var(--on-sfv);
}

.ut-row .ut-role .role-tag {
  display: inline-flex;
  padding: 3px 10px;
  border-radius: var(--r6);
  font: 500 11px/16px 'Roboto', sans-serif;
}

.role-tag.admin {
  background: rgba(200,191,255,.15);
  color: var(--pri);
}

.role-tag.operator {
  background: rgba(255,158,67,.12);
  color: var(--orange);
}

.ut-row .ut-status {
  display: flex;
  align-items: center;
  gap: 5px;
  font: 500 12px/16px 'Roboto', sans-serif;
}

.ut-row .ut-status .dot {
  width: 6px;
  height: 6px;
  border-radius: 50%;
}

.ut-row .ut-actions {
  display: flex;
  gap: 4px;
}

.ut-row .ut-actions .act-btn {
  width: 28px;
  height: 28px;
  border-radius: 50%;
  border: none;
  background: transparent;
  display: flex;
  align-items: center;
  justify-content: center;
  cursor: pointer;
  transition: background .15s;
}

.ut-row .ut-actions .act-btn:hover {
  background: var(--sc3);
}

.ut-row .ut-actions .act-btn .mi {
  font-size: 18px;
  color: var(--on-sfv);
}

.audit-row {
  display: grid;
  grid-template-columns: 140px 1fr 140px 100px;
  align-items: center;
  padding: 10px 0;
  border-bottom: 1px solid var(--olv);
  gap: 12px;
  font: 400 13px/18px 'Roboto', sans-serif;
}

.audit-row:last-child {
  border-bottom: none;
}

.audit-row.header {
  font: 500 12px/16px 'Roboto', sans-serif;
  color: var(--on-sfv);
  text-transform: uppercase;
  letter-spacing: .5px;
}

.audit-row .au-time {
  color: var(--ol);
}

.audit-row .au-action {
  display: flex;
  align-items: center;
  gap: 8px;
  min-width: 0;
}

.audit-row .au-action .mi {
  font-size: 16px;
  color: var(--on-sfv);
  flex-shrink: 0;
}

.audit-row .au-user {
  color: var(--on-sfv);
}

.audit-row .au-type .type-tag {
  display: inline-flex;
  padding: 2px 8px;
  border-radius: var(--r6);
  font: 500 11px/16px 'Roboto', sans-serif;
}

.type-tag.login {
  background: rgba(200,191,255,.12);
  color: var(--pri);
}

.type-tag.config {
  background: rgba(255,158,67,.12);
  color: var(--orange);
}

.type-tag.device {
  background: rgba(125,216,129,.12);
  color: var(--green);
}

.notif-channel {
  display: flex;
  align-items: center;
  gap: 14px;
  padding: 14px 0;
  border-bottom: 1px solid var(--olv);
}

.notif-channel:last-child {
  border-bottom: none;
}

.notif-channel .nc-icon {
  width: 40px;
  height: 40px;
  border-radius: var(--r2);
  display: flex;
  align-items: center;
  justify-content: center;
  flex-shrink: 0;
  background: var(--sc2);
}

.notif-channel .nc-icon .mi {
  font-size: 20px;
  color: var(--on-sfv);
}

.notif-channel .nc-info {
  flex: 1;
  min-width: 0;
}

.notif-channel .nc-info h4 {
  font: 500 14px/20px 'Roboto', sans-serif;
}

.notif-channel .nc-info p {
  font: 400 12px/16px 'Roboto', sans-serif;
  color: var(--on-sfv);
}

.notif-channel .nc-status {
  font: 500 12px/16px 'Roboto', sans-serif;
  color: var(--green);
}

.notif-channel .nc-status.off {
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

.about-item .ai-label {
  font: 400 12px/16px 'Roboto', sans-serif;
  color: var(--on-sfv);
  margin-bottom: 4px;
}

.about-item .ai-value {
  font: 500 14px/20px 'Roboto', sans-serif;
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
    line-height: 16px;
  }

  .form-grid {
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

  .about-grid {
    grid-template-columns: 1fr;
  }
}
</style>
