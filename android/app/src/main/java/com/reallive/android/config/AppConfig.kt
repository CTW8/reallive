package com.reallive.android.config

import android.content.Context

class AppConfig(context: Context) {
    private val sp = context.getSharedPreferences("reallive_app", Context.MODE_PRIVATE)

    fun getBaseUrl(): String {
        val raw = sp.getString(KEY_BASE_URL, DEFAULT_BASE_URL)?.trim().orEmpty()
        val normalized = if (raw.endsWith('/')) raw else "$raw/"
        return normalized
    }

    fun setBaseUrl(baseUrl: String) {
        val normalized = if (baseUrl.endsWith('/')) baseUrl else "$baseUrl/"
        sp.edit().putString(KEY_BASE_URL, normalized).apply()
    }

    fun getToken(): String? = sp.getString(KEY_TOKEN, null)

    fun setToken(token: String?) {
        sp.edit().putString(KEY_TOKEN, token).apply()
    }

    companion object {
        private const val KEY_BASE_URL = "base_url"
        private const val KEY_TOKEN = "token"

        // Update this on real device if needed.
        private const val DEFAULT_BASE_URL = "http://192.168.1.10/"
    }
}
