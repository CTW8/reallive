<script setup>
import { ref } from 'vue'
import { useRouter } from 'vue-router'
import { useAuthStore } from '../stores/auth.js'

const router = useRouter()
const auth = useAuthStore()

const email = ref('')
const password = ref('')
const rememberMe = ref(true)
const showPassword = ref(false)
const error = ref('')
const loading = ref(false)

async function handleLogin() {
  error.value = ''
  loading.value = true
  try {
    await auth.login(email.value, password.value)
    router.push('/')
  } catch (err) {
    error.value = err.message || 'Login failed'
  } finally {
    loading.value = false
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
            <a class="forgot" href="#">Forgot password?</a>
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
          Don't have an account? <router-link to="/register">Contact Admin</router-link>
        </div>
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
