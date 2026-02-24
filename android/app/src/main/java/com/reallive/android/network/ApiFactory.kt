package com.reallive.android.network

import com.reallive.android.config.AppConfig

object ApiFactory {
    fun createAuthorized(appConfig: AppConfig): RealLiveApi {
        return ApiClient.create(
            baseUrl = appConfig.getBaseUrl(),
            tokenProvider = appConfig::getToken,
            refreshTokenProvider = appConfig::getRefreshToken,
            onTokenRefreshed = { token, refreshToken ->
                appConfig.setAuthTokens(token, refreshToken ?: appConfig.getRefreshToken())
                appConfig.markAuthenticated()
            },
            onRefreshFailed = {
                appConfig.clearAuth()
            },
        )
    }
}

