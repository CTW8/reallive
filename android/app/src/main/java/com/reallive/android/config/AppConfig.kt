package com.reallive.android.config

import android.content.Context
import android.util.Base64
import org.json.JSONObject

class AppConfig(context: Context) {
    private val sp = context.getSharedPreferences("reallive_app", Context.MODE_PRIVATE)

    fun getBaseUrl(): String {
        val raw = DEFAULT_BASE_URL.trim()
        val normalized = if (raw.endsWith('/')) raw else "$raw/"
        return normalized
    }

    fun getToken(): String? = sp.getString(KEY_TOKEN, null)

    fun setToken(token: String?) {
        sp.edit().putString(KEY_TOKEN, token).apply()
    }

    fun getUserId(): Long {
        val cached = sp.getLong(KEY_USER_ID, -1L)
        if (cached > 0L) return cached
        val token = getToken() ?: return -1L
        val parsed = parseUserIdFromToken(token)
        if (parsed > 0L) {
            setUserId(parsed)
        }
        return parsed
    }

    fun setUserId(userId: Long) {
        sp.edit().putLong(KEY_USER_ID, userId).apply()
    }

    fun getUsername(): String? = sp.getString(KEY_USERNAME, null)

    fun setUsername(username: String?) {
        sp.edit().putString(KEY_USERNAME, username).apply()
    }

    fun getEmail(): String? = sp.getString(KEY_EMAIL, null)

    fun setEmail(email: String?) {
        sp.edit().putString(KEY_EMAIL, email).apply()
    }

    fun getAutoLockSec(): Int {
        val sec = sp.getInt(KEY_AUTO_LOCK_SEC, 60)
        return if (sec < 0) 0 else sec
    }

    fun getAppLanguage(): String {
        return sp.getString(KEY_APP_LANGUAGE, "en") ?: "en"
    }

    fun setAppLanguage(languageCode: String?) {
        val code = languageCode?.trim().takeUnless { it.isNullOrBlank() } ?: "en"
        sp.edit().putString(KEY_APP_LANGUAGE, code).apply()
    }

    fun setAutoLockSec(seconds: Int) {
        sp.edit().putInt(KEY_AUTO_LOCK_SEC, seconds.coerceAtLeast(0)).apply()
    }

    fun markAppBackgrounded(nowMs: Long = System.currentTimeMillis()) {
        sp.edit().putLong(KEY_LAST_BACKGROUND_AT_MS, nowMs).apply()
    }

    fun markAuthenticated(nowMs: Long = System.currentTimeMillis()) {
        sp.edit().putLong(KEY_LAST_AUTH_AT_MS, nowMs).apply()
    }

    fun shouldRequireReauth(nowMs: Long = System.currentTimeMillis()): Boolean {
        if (getToken().isNullOrBlank()) return false
        val autoLockSec = getAutoLockSec()
        if (autoLockSec <= 0) return false
        val lastBgMs = sp.getLong(KEY_LAST_BACKGROUND_AT_MS, 0L)
        if (lastBgMs <= 0L) return false
        val lastAuthMs = sp.getLong(KEY_LAST_AUTH_AT_MS, 0L)
        if (lastAuthMs > 0L && lastAuthMs >= lastBgMs) return false
        val elapsed = nowMs - lastBgMs
        return elapsed >= autoLockSec * 1000L
    }

    fun clearAuth() {
        sp.edit()
            .remove(KEY_TOKEN)
            .remove(KEY_USER_ID)
            .remove(KEY_USERNAME)
            .remove(KEY_EMAIL)
            .remove(KEY_LAST_AUTH_AT_MS)
            .remove(KEY_LAST_BACKGROUND_AT_MS)
            .apply()
    }

    private fun parseUserIdFromToken(token: String): Long {
        val parts = token.split('.')
        if (parts.size < 2) return -1L
        return runCatching {
            val payload = parts[1]
            val decoded = Base64.decode(payload, Base64.URL_SAFE or Base64.NO_WRAP or Base64.NO_PADDING)
            val json = JSONObject(String(decoded))
            json.optLong("id", -1L)
        }.getOrDefault(-1L)
    }

    companion object {
        private const val KEY_TOKEN = "token"
        private const val KEY_USER_ID = "user_id"
        private const val KEY_USERNAME = "username"
        private const val KEY_EMAIL = "email"
        private const val KEY_AUTO_LOCK_SEC = "auto_lock_sec"
        private const val KEY_APP_LANGUAGE = "app_language"
        private const val KEY_LAST_AUTH_AT_MS = "last_auth_at_ms"
        private const val KEY_LAST_BACKGROUND_AT_MS = "last_bg_at_ms"

        private const val DEFAULT_BASE_URL = "http://192.168.5.200/"
    }
}
