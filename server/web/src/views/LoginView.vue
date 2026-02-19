<script setup>
import { ref } from 'vue'
import { useRouter } from 'vue-router'
import { useAuthStore } from '../stores/auth.js'
import { authApi } from '../api/index.js'

const router = useRouter()
const auth = useAuthStore()

const email = ref('')
const password = ref('')
const rememberMe = ref(true)
const showPassword = ref(false)
const error = ref('')
const loading = ref(false)
const resetOpen = ref(false)
const resetEmail = ref('')
const resetLoading = ref(false)
const resetError = ref('')
const tempPassword = ref('')

async function handleLogin() {
  error.value = ''
  loading.value = true
  try {
    await auth.login(email.value, password.value)
    router.push('/dashboard')
  } catch (err) {
    error.value = err.message || 'Login failed'
  } finally {
    loading.value = false
  }
}

async function handleForgotPassword() {
  resetError.value = ''
  tempPassword.value = ''
  resetLoading.value = true
  try {
    const data = await authApi.forgotPassword(resetEmail.value)
    tempPassword.value = data?.temporaryPassword || ''
  } catch (err) {
    resetError.value = err?.message || 'Failed to reset password'
  } finally {
    resetLoading.value = false
  }
}

async function copyTempPassword() {
  if (!tempPassword.value) return
  try {
    await navigator.clipboard.writeText(tempPassword.value)
  } catch {
  }
}
</script>

<template>
  <div class="login-page">
    <div class="login-brand">
      <div class="logo-icon">
        <span class="mi">linked_camera</span>
      </div>
      <h1>RealLive</h1>
      <p>Professional remote monitoring console for home & campus security</p>
      <div class="features">
        <div class="feat">
          <span class="mi">videocam</span>
          <span>Real-time</span>
        </div>
        <div class="feat">
          <span class="mi">notifications_active</span>
          <span>Smart Alerts</span>
        </div>
        <div class="feat">
          <span class="mi">cloud_done</span>
          <span>Cloud Storage</span>
        </div>
        <div class="feat">
          <span class="mi">devices</span>
          <span>Multi-device</span>
        </div>
      </div>
    </div>

    <div class="login-form-side">
      <div class="login-card">
        <h2>Welcome Back</h2>
        <p class="sub">Sign in to your monitoring console</p>

        <form @submit.prevent="handleLogin">
          <div class="form-field">
            <label>Email</label>
            <input
              v-model="email"
              type="email"
              placeholder="admin@reallive.com"
              required
              autocomplete="email"
            >
            <span class="mi field-icon">mail</span>
          </div>
          <div class="form-field">
            <label>Password</label>
            <input
              v-model="password"
              :type="showPassword ? 'text' : 'password'"
              placeholder="Enter password"
              required
              autocomplete="current-password"
            >
            <span class="mi field-icon" @click="showPassword = !showPassword">
              {{ showPassword ? 'visibility' : 'visibility_off' }}
            </span>
          </div>
          <div class="form-row">
            <label class="remember">
              <input v-model="rememberMe" type="checkbox"> Remember me
            </label>
            <button class="forgot forgot-btn" type="button" @click="resetOpen = true">Forgot password?</button>
          </div>
          <p v-if="error" class="error-msg">{{ error }}</p>
          <button class="btn-primary" type="submit" :disabled="loading">
            {{ loading ? 'Signing in...' : 'Sign In' }}
          </button>
        </form>

        <div class="login-divider"><span>or continue with</span></div>
        <div class="social-row">
          <button class="social-btn" type="button"><span class="mi" style="font-size:18px">public</span> Google</button>
          <button class="social-btn" type="button"><span class="mi" style="font-size:18px">domain</span> SSO</button>
        </div>
        <div class="login-footer">
          Don't have an account? <router-link to="/register">Create account</router-link>
        </div>
      </div>
    </div>

    <div v-if="resetOpen" class="reset-mask" @click.self="resetOpen = false">
      <div class="reset-modal">
        <div class="reset-head">
          <h3>Reset Password</h3>
          <button class="close-btn" type="button" @click="resetOpen = false"><span class="mi">close</span></button>
        </div>
        <form class="reset-body" @submit.prevent="handleForgotPassword">
          <label>Email</label>
          <input
            v-model="resetEmail"
            type="email"
            placeholder="your-email@example.com"
            required
          >
          <p v-if="resetError" class="reset-error">{{ resetError }}</p>
          <div v-if="tempPassword" class="reset-result">
            <p>Temporary password:</p>
            <code>{{ tempPassword }}</code>
            <button type="button" class="copy-btn" @click="copyTempPassword">Copy</button>
          </div>
          <button class="btn-primary" type="submit" :disabled="resetLoading">
            {{ resetLoading ? 'Generating...' : 'Generate Temporary Password' }}
          </button>
        </form>
      </div>
    </div>
  </div>
</template>

<style scoped>
.login-page {
  width: 100%;
  height: 100%;
  display: flex;
  justify-content: center;
  gap: clamp(72px, 8vw, 168px);
  background: var(--sc0);
  position: relative;
  overflow: hidden;
}

.login-page::before {
  content: '';
  position: absolute;
  width: 600px;
  height: 600px;
  border-radius: 50%;
  background: var(--pri);
  filter: blur(180px);
  opacity: .06;
  top: -200px;
  left: -100px;
}

.login-page::after {
  content: '';
  position: absolute;
  width: 500px;
  height: 500px;
  border-radius: 50%;
  background: var(--ter);
  filter: blur(160px);
  opacity: .05;
  bottom: -200px;
  right: -100px;
}

.login-brand {
  flex: 0 1 680px;
  display: flex;
  flex-direction: column;
  align-items: flex-start;
  justify-content: center;
  position: relative;
  z-index: 1;
  padding: 40px 40px 40px clamp(36px, 6vw, 120px);
}

.logo-icon {
  width: 88px;
  height: 88px;
  border-radius: 24px;
  background: linear-gradient(135deg, var(--pri), var(--ter));
  display: flex;
  align-items: center;
  justify-content: center;
  margin-bottom: 28px;
  box-shadow: 0 12px 40px rgba(100,66,214,.3);
}

.logo-icon .mi {
  font-size: 44px;
  color: #fff;
  font-variation-settings: 'FILL' 1, 'wght' 400, 'GRAD' 0, 'opsz' 48;
}

.login-brand h1 {
  font: 500 36px/44px 'Roboto', sans-serif;
  color: #fff;
  margin-bottom: 8px;
}

.login-brand p {
  font: 400 16px/24px 'Roboto', sans-serif;
  color: var(--on-sfv);
  text-align: left;
  max-width: 420px;
}

.login-brand .features {
  display: flex;
  gap: 28px;
  margin-top: 48px;
}

.login-brand .feat {
  display: flex;
  flex-direction: column;
  align-items: flex-start;
  gap: 8px;
}

.login-brand .feat .mi {
  font-size: 28px;
  color: var(--pri);
}

.login-brand .feat span {
  font: 400 13px/18px 'Roboto', sans-serif;
  color: var(--on-sfv);
}

.login-form-side {
  width: 520px;
  display: flex;
  align-items: center;
  justify-content: center;
  position: relative;
  z-index: 1;
  padding: 40px;
}

.login-card {
  width: 100%;
  max-width: 380px;
  background: var(--sc);
  border-radius: var(--r4);
  padding: 36px 32px;
  box-shadow: var(--e3);
}

.login-card h2 {
  font: 500 24px/32px 'Roboto', sans-serif;
  margin-bottom: 4px;
}

.login-card .sub {
  font: 400 14px/20px 'Roboto', sans-serif;
  color: var(--on-sfv);
  margin-bottom: 28px;
}

.form-row {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 24px;
}

.form-row .remember {
  display: flex;
  align-items: center;
  gap: 8px;
  font: 400 13px/18px 'Roboto', sans-serif;
  color: var(--on-sfv);
  cursor: pointer;
}

.form-row .remember input[type="checkbox"] {
  width: 16px;
  height: 16px;
  accent-color: var(--pri);
}

.form-row .forgot {
  font: 500 13px/18px 'Roboto', sans-serif;
  color: var(--pri);
  text-decoration: none;
}

.forgot-btn {
  border: none;
  background: transparent;
  cursor: pointer;
}

.form-row .forgot:hover {
  text-decoration: underline;
}

.error-msg {
  color: var(--red);
  font-size: 13px;
  margin-bottom: 12px;
}

.login-divider {
  display: flex;
  align-items: center;
  gap: 14px;
  margin: 24px 0;
}

.login-divider::before, .login-divider::after {
  content: '';
  flex: 1;
  height: 1px;
  background: var(--olv);
}

.login-divider span {
  font: 400 12px/16px 'Roboto', sans-serif;
  color: var(--on-sfv);
  white-space: nowrap;
}

.social-row {
  display: flex;
  gap: 10px;
}

.social-btn {
  flex: 1;
  height: 42px;
  border: 1px solid var(--olv);
  border-radius: var(--r6);
  background: transparent;
  display: flex;
  align-items: center;
  justify-content: center;
  gap: 8px;
  font: 500 13px/18px 'Roboto', sans-serif;
  color: var(--on-sf);
  cursor: pointer;
  transition: background .2s;
}

.social-btn:hover {
  background: var(--sc2);
}

.login-footer {
  text-align: center;
  margin-top: 24px;
  font: 400 14px/20px 'Roboto', sans-serif;
  color: var(--on-sfv);
}

.login-footer a {
  color: var(--pri);
  text-decoration: none;
  font-weight: 500;
}

.login-footer a:hover {
  text-decoration: underline;
}

.reset-mask {
  position: fixed;
  inset: 0;
  background: rgba(3, 7, 18, .65);
  display: flex;
  align-items: center;
  justify-content: center;
  z-index: 30;
}

.reset-modal {
  width: min(420px, calc(100% - 24px));
  background: var(--sc);
  border-radius: var(--r3);
  border: 1px solid var(--olv);
  box-shadow: var(--e3);
}

.reset-head {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 14px 16px;
  border-bottom: 1px solid var(--olv);
}

.reset-head h3 {
  font: 500 16px/22px 'Roboto', sans-serif;
}

.close-btn {
  width: 28px;
  height: 28px;
  border-radius: 50%;
  border: 1px solid var(--olv);
  background: transparent;
  color: var(--on-sfv);
}

.reset-body {
  padding: 14px 16px 16px;
  display: grid;
  gap: 10px;
}

.reset-body label {
  font: 500 12px/16px 'Roboto', sans-serif;
  color: var(--on-sfv);
}

.reset-body input {
  width: 100%;
  height: 40px;
  border: 1px solid var(--olv);
  border-radius: var(--r2);
  padding: 0 10px;
  background: var(--sc2);
  color: var(--on-sf);
}

.reset-error {
  color: #ffb4b4;
  font: 400 12px/16px 'Roboto', sans-serif;
}

.reset-result {
  border: 1px solid rgba(125,216,129,.4);
  background: rgba(125,216,129,.1);
  border-radius: var(--r2);
  padding: 10px;
  display: grid;
  gap: 6px;
}

.reset-result p {
  font: 400 12px/16px 'Roboto', sans-serif;
  color: var(--on-sfv);
}

.reset-result code {
  font: 600 14px/20px ui-monospace, SFMono-Regular, Menlo, monospace;
  color: #bbf7d0;
}

.copy-btn {
  width: 72px;
  height: 28px;
  border-radius: var(--r6);
  border: 1px solid var(--olv);
  background: transparent;
  color: var(--on-sf);
  font: 500 12px/16px 'Roboto', sans-serif;
}

@media (max-width: 1023px) {
  .login-brand {
    display: none;
  }

  .login-form-side {
    width: 100%;
    padding: 24px;
  }
}
</style>
