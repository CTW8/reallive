package com.reallive.android.network

import okhttp3.Interceptor
import okhttp3.MediaType.Companion.toMediaType
import okhttp3.OkHttpClient
import okhttp3.RequestBody.Companion.toRequestBody
import okhttp3.logging.HttpLoggingInterceptor
import org.json.JSONObject
import retrofit2.Retrofit
import retrofit2.converter.gson.GsonConverterFactory
import java.util.concurrent.TimeUnit

object ApiClient {
    private val refreshLock = Any()

    private enum class RefreshFailReason {
        NONE,
        INVALID_TOKEN,
        TRANSIENT,
    }

    fun create(
        baseUrl: String,
        tokenProvider: () -> String?,
        refreshTokenProvider: () -> String? = { null },
        onTokenRefreshed: ((String, String?) -> Unit)? = null,
        onRefreshFailed: (() -> Unit)? = null,
    ): RealLiveApi {
        val refreshHttp = OkHttpClient.Builder()
            .connectTimeout(8, TimeUnit.SECONDS)
            .readTimeout(8, TimeUnit.SECONDS)
            .writeTimeout(8, TimeUnit.SECONDS)
            .build()

        var lastRefreshFailReason = RefreshFailReason.NONE

        fun refreshAccessTokenIfNeeded(tokenUsed: String?): String? {
            val latest = tokenProvider()
            if (!latest.isNullOrBlank() && latest != tokenUsed) return latest
            synchronized(refreshLock) {
                lastRefreshFailReason = RefreshFailReason.NONE
                val newest = tokenProvider()
                if (!newest.isNullOrBlank() && newest != tokenUsed) return newest
                val refreshToken = refreshTokenProvider().orEmpty()
                if (refreshToken.isBlank()) {
                    lastRefreshFailReason = RefreshFailReason.INVALID_TOKEN
                    return null
                }
                return runCatching {
                    val json = JSONObject().put("refreshToken", refreshToken).toString()
                    val req = okhttp3.Request.Builder()
                        .url(baseUrl.trimEnd('/') + "/api/auth/refresh")
                        .post(json.toRequestBody("application/json; charset=utf-8".toMediaType()))
                        .build()
                    refreshHttp.newCall(req).execute().use { resp ->
                        if (!resp.isSuccessful) {
                            lastRefreshFailReason = when (resp.code) {
                                400, 401, 403 -> RefreshFailReason.INVALID_TOKEN
                                else -> RefreshFailReason.TRANSIENT
                            }
                            return null
                        }
                        val body = resp.body?.string().orEmpty()
                        if (body.isBlank()) {
                            lastRefreshFailReason = RefreshFailReason.TRANSIENT
                            return null
                        }
                        val obj = JSONObject(body)
                        val newToken = obj.optString("token")
                        if (newToken.isBlank()) {
                            lastRefreshFailReason = RefreshFailReason.TRANSIENT
                            return null
                        }
                        val newRefresh = obj.optString("refreshToken").takeIf { it.isNotBlank() }
                        onTokenRefreshed?.invoke(newToken, newRefresh)
                        lastRefreshFailReason = RefreshFailReason.NONE
                        newToken
                    }
                }.getOrElse {
                    lastRefreshFailReason = RefreshFailReason.TRANSIENT
                    null
                }
            }
        }

        val authInterceptor = Interceptor { chain ->
            val token = tokenProvider()
            val request = if (!token.isNullOrBlank()) {
                chain.request().newBuilder()
                    .addHeader("Authorization", "Bearer $token")
                    .build()
            } else {
                chain.request()
            }
            val response = chain.proceed(request)
            if (response.code != 401 || request.header("X-Auth-Retry") == "1") {
                return@Interceptor response
            }

            val refreshedToken = refreshAccessTokenIfNeeded(token)
            if (refreshedToken.isNullOrBlank()) {
                if (lastRefreshFailReason == RefreshFailReason.INVALID_TOKEN) {
                    onRefreshFailed?.invoke()
                }
                return@Interceptor response
            }

            response.close()
            val retry = request.newBuilder()
                .header("Authorization", "Bearer $refreshedToken")
                .header("X-Auth-Retry", "1")
                .build()
            chain.proceed(retry)
        }

        val logging = HttpLoggingInterceptor().apply {
            level = HttpLoggingInterceptor.Level.BASIC
        }

        val http = OkHttpClient.Builder()
            .addInterceptor(authInterceptor)
            .addInterceptor(logging)
            .build()

        val retrofit = Retrofit.Builder()
            .baseUrl(baseUrl)
            .client(http)
            .addConverterFactory(GsonConverterFactory.create())
            .build()

        return retrofit.create(RealLiveApi::class.java)
    }
}
