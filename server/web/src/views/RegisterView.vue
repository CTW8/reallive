<script setup>
import { ref } from 'vue'
import { useRouter } from 'vue-router'
import { useAuthStore } from '../stores/auth.js'

const router = useRouter()
const auth = useAuthStore()

const username = ref('')
const email = ref('')
const password = ref('')
const showPassword = ref(false)
const agreeTerms = ref(true)
const error = ref('')
const loading = ref(false)

async function handleRegister() {
  error.value = ''
  loading.value = true
  try {
    await auth.register(username.value, email.value, password.value)
    router.push('/login')
  } catch (err) {
    error.value = err.message || 'Registration failed'
  } finally {
    loading.value = false
  }
}
</script>

<template>
  <div class="register-page">
    <div class="register-brand">
      <div class="logo-icon">
        <span class="mi">linked_camera</span>
      </div>
      <h1>RealLive</h1>
      <p>Create a new operator account for remote monitoring and incident response.</p>
      <div class="features">
        <div class="feat">
          <span class="mi">verified_user</span>
          <span>Secure Access</span>
        </div>
        <div class="feat">
          <span class="mi">groups</span>
          <span>Role-based</span>
        </div>
        <div class="feat">
          <span class="mi">history</span>
          <span>Audit Trail</span>
        </div>
      </div>
    </div>

    <div class="register-form-side">
      <div class="register-card">
        <h2>Create Account</h2>
        <p class="sub">Set up your RealLive console account</p>

        <form @submit.prevent="handleRegister">
          <div class="form-field">
            <label>Username</label>
            <input
              v-model="username"
              type="text"
              placeholder="Choose a username"
              required
              autocomplete="username"
            >
            <span class="mi field-icon">person</span>
          </div>

          <div class="form-field">
            <label>Email</label>
            <input
              v-model="email"
              type="email"
              placeholder="operator@reallive.com"
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
              placeholder="Create password"
              required
              autocomplete="new-password"
            >
            <span class="mi field-icon" @click="showPassword = !showPassword">
              {{ showPassword ? 'visibility' : 'visibility_off' }}
            </span>
          </div>

          <div class="form-row">
            <label class="remember">
              <input v-model="agreeTerms" type="checkbox" required>
              I agree to terms and policy
            </label>
          </div>

          <p v-if="error" class="error-msg">{{ error }}</p>

          <button class="btn-primary" type="submit" :disabled="loading || !agreeTerms">
            {{ loading ? 'Creating account...' : 'Create Account' }}
          </button>
        </form>

        <div class="register-footer">
          Already have an account? <router-link to="/login">Sign in</router-link>
        </div>
      </div>
    </div>
  </div>
</template>

<style scoped>
.register-page {
  width: 100%;
  height: 100%;
  display: flex;
  justify-content: center;
  gap: clamp(72px, 8vw, 168px);
  background: var(--sc0);
  position: relative;
  overflow: hidden;
}

.register-page::before {
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

.register-page::after {
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

.register-brand {
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

.register-brand h1 {
  font: 500 36px/44px 'Roboto', sans-serif;
  color: #fff;
  margin-bottom: 8px;
}

.register-brand p {
  font: 400 16px/24px 'Roboto', sans-serif;
  color: var(--on-sfv);
  text-align: left;
  max-width: 440px;
}

.features {
  display: flex;
  gap: 28px;
  margin-top: 48px;
}

.feat {
  display: flex;
  flex-direction: column;
  align-items: flex-start;
  gap: 8px;
}

.feat .mi {
  font-size: 28px;
  color: var(--pri);
}

.feat span {
  font: 400 13px/18px 'Roboto', sans-serif;
  color: var(--on-sfv);
}

.register-form-side {
  width: 520px;
  display: flex;
  align-items: center;
  justify-content: center;
  position: relative;
  z-index: 1;
  padding: 40px;
}

.register-card {
  width: 100%;
  max-width: 400px;
  background: var(--sc);
  border-radius: var(--r4);
  padding: 36px 32px;
  box-shadow: var(--e3);
}

.register-card h2 {
  font: 500 24px/32px 'Roboto', sans-serif;
  margin-bottom: 4px;
}

.register-card .sub {
  font: 400 14px/20px 'Roboto', sans-serif;
  color: var(--on-sfv);
  margin-bottom: 24px;
}

.form-field {
  margin-bottom: 14px;
  position: relative;
}

.form-field label {
  font: 500 12px/16px 'Roboto', sans-serif;
  color: var(--on-sfv);
  margin-bottom: 6px;
  display: block;
}

.form-field input {
  width: 100%;
  height: 42px;
  border: 1px solid var(--olv);
  border-radius: var(--r2);
  padding: 0 38px 0 12px;
  background: var(--sc2);
  color: var(--on-sf);
  font: 400 14px/20px 'Roboto', sans-serif;
  outline: none;
}

.form-field input:focus {
  border-color: var(--pri);
}

.form-field .field-icon {
  position: absolute;
  right: 12px;
  top: 34px;
  font-size: 18px;
  color: var(--ol);
  cursor: pointer;
}

.form-row {
  display: flex;
  align-items: center;
  margin-bottom: 16px;
}

.remember {
  display: flex;
  align-items: center;
  gap: 8px;
  font: 400 13px/18px 'Roboto', sans-serif;
  color: var(--on-sfv);
  cursor: pointer;
}

.remember input {
  width: 16px;
  height: 16px;
  accent-color: var(--pri);
}

.error-msg {
  margin-bottom: 12px;
  border: 1px solid rgba(255,107,107,.35);
  background: rgba(255,107,107,.12);
  color: #ffb4b4;
  border-radius: var(--r2);
  padding: 8px 10px;
  font: 400 12px/16px 'Roboto', sans-serif;
}

.btn-primary {
  width: 100%;
  height: 42px;
  border: none;
  border-radius: var(--r6);
  background: var(--pri);
  color: var(--on-pri);
  font: 500 14px/20px 'Roboto', sans-serif;
  cursor: pointer;
}

.btn-primary:disabled {
  opacity: .55;
  cursor: not-allowed;
}

.register-footer {
  margin-top: 18px;
  text-align: center;
  font: 400 13px/18px 'Roboto', sans-serif;
  color: var(--on-sfv);
}

.register-footer a {
  color: var(--pri);
  text-decoration: none;
}

@media (max-width: 1023px) {
  .register-page {
    flex-direction: column;
    gap: 20px;
    overflow: auto;
  }

  .register-brand {
    flex: none;
    width: 100%;
    align-items: center;
    text-align: center;
    padding: 36px 20px 8px;
  }

  .register-brand p {
    text-align: center;
  }

  .features {
    margin-top: 24px;
    justify-content: center;
    flex-wrap: wrap;
    gap: 18px;
  }

  .feat {
    align-items: center;
  }

  .register-form-side {
    width: 100%;
    padding: 16px 20px 28px;
  }

  .register-card {
    max-width: 420px;
  }
}
</style>
