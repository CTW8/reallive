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

    fun clearAuth() {
        sp.edit()
            .remove(KEY_TOKEN)
            .remove(KEY_USER_ID)
            .remove(KEY_USERNAME)
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

        private const val DEFAULT_BASE_URL = "http://192.168.5.200/"
    }
}
