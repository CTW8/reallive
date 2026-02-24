package com.reallive.android.config

import android.content.Context
import android.content.SharedPreferences
import android.util.Base64
import androidx.security.crypto.EncryptedSharedPreferences
import androidx.security.crypto.MasterKey
import org.json.JSONObject

class AppConfig(context: Context) {
    private val sp: SharedPreferences = runCatching {
        val masterKey = MasterKey.Builder(context)
            .setKeyScheme(MasterKey.KeyScheme.AES256_GCM)
            .build()
        EncryptedSharedPreferences.create(
            context,
            "reallive_app_secure",
            masterKey,
            EncryptedSharedPreferences.PrefKeyEncryptionScheme.AES256_SIV,
            EncryptedSharedPreferences.PrefValueEncryptionScheme.AES256_GCM,
        )
    }.getOrElse {
        context.getSharedPreferences("reallive_app", Context.MODE_PRIVATE)
    }

    fun getBaseUrl(): String {
        val raw = DEFAULT_BASE_URL.trim()
        val normalized = if (raw.endsWith('/')) raw else "$raw/"
        return normalized
    }

    fun getToken(): String? = sp.getString(KEY_TOKEN, null)
    fun getRefreshToken(): String? = sp.getString(KEY_REFRESH_TOKEN, null)

    fun setAuthTokens(token: String?, refreshToken: String?) {
        setToken(token)
        setRefreshToken(refreshToken)
    }

    fun setToken(token: String?) {
        val editor = sp.edit().putString(KEY_TOKEN, token)
        if (token.isNullOrBlank()) {
            editor.remove(KEY_LOGIN_AT_MS).remove(KEY_SESSION_EXPIRES_AT_MS)
        } else {
            val now = System.currentTimeMillis()
            editor.putLong(KEY_LOGIN_AT_MS, now)
            editor.putLong(KEY_SESSION_EXPIRES_AT_MS, now + SESSION_VALID_MS)
        }
        editor.apply()
    }

    fun setRefreshToken(refreshToken: String?) {
        val editor = sp.edit()
        if (refreshToken.isNullOrBlank()) {
            editor.remove(KEY_REFRESH_TOKEN)
        } else {
            editor.putString(KEY_REFRESH_TOKEN, refreshToken)
        }
        editor.apply()
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
        val editor = sp.edit().putLong(KEY_LAST_AUTH_AT_MS, nowMs)
        if (!getToken().isNullOrBlank()) {
            val expiresAt = sp.getLong(KEY_SESSION_EXPIRES_AT_MS, 0L)
            if (expiresAt <= 0L) {
                editor.putLong(KEY_LOGIN_AT_MS, nowMs)
                editor.putLong(KEY_SESSION_EXPIRES_AT_MS, nowMs + SESSION_VALID_MS)
            }
        }
        editor.apply()
    }

    fun shouldRequireReauth(nowMs: Long = System.currentTimeMillis()): Boolean {
        if (getToken().isNullOrBlank()) return false
        val expiresAt = sp.getLong(KEY_SESSION_EXPIRES_AT_MS, 0L)
        if (expiresAt <= 0L) return false
        return nowMs >= expiresAt
    }

    fun clearAuth() {
        val editor = sp.edit()
        val oldUserId = getUserId()
        if (oldUserId > 0L) {
            sp.all.keys
                .filter { it.startsWith("${KEY_ADD_SOURCE_HINT_PREFIX}${oldUserId}_") }
                .forEach { editor.remove(it) }
        }
        editor
            .remove(KEY_TOKEN)
            .remove(KEY_USER_ID)
            .remove(KEY_USERNAME)
            .remove(KEY_EMAIL)
            .remove(KEY_REFRESH_TOKEN)
            .remove(KEY_LAST_AUTH_AT_MS)
            .remove(KEY_LAST_BACKGROUND_AT_MS)
            .remove(KEY_LOGIN_AT_MS)
            .remove(KEY_SESSION_EXPIRES_AT_MS)
            .apply()
    }

    fun isAddSourceHintShown(cameraId: Long): Boolean {
        val userId = getUserId()
        if (userId <= 0L || cameraId <= 0L) return false
        return sp.getBoolean("${KEY_ADD_SOURCE_HINT_PREFIX}${userId}_$cameraId", false)
    }

    fun markAddSourceHintShown(cameraId: Long) {
        val userId = getUserId()
        if (userId <= 0L || cameraId <= 0L) return
        sp.edit().putBoolean("${KEY_ADD_SOURCE_HINT_PREFIX}${userId}_$cameraId", true).apply()
    }

    fun clearAddSourceHintsForCurrentUser() {
        val userId = getUserId()
        if (userId <= 0L) return
        val editor = sp.edit()
        sp.all.keys
            .filter { it.startsWith("${KEY_ADD_SOURCE_HINT_PREFIX}${userId}_") }
            .forEach { editor.remove(it) }
        editor.apply()
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
        private const val KEY_REFRESH_TOKEN = "refresh_token"
        private const val KEY_AUTO_LOCK_SEC = "auto_lock_sec"
        private const val KEY_APP_LANGUAGE = "app_language"
        private const val KEY_LAST_AUTH_AT_MS = "last_auth_at_ms"
        private const val KEY_LAST_BACKGROUND_AT_MS = "last_bg_at_ms"
        private const val KEY_LOGIN_AT_MS = "login_at_ms"
        private const val KEY_SESSION_EXPIRES_AT_MS = "session_expires_at_ms"
        private const val KEY_ADD_SOURCE_HINT_PREFIX = "add_source_hint_shown_"

        private const val DEFAULT_BASE_URL = "http://192.168.5.200/"
        private const val SESSION_VALID_MS = 30L * 24L * 60L * 60L * 1000L
    }
}
