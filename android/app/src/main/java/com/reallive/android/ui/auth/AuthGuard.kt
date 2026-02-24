package com.reallive.android.ui.auth

import com.reallive.android.config.AppConfig
import com.reallive.android.data.CameraRepository
import com.reallive.android.network.ApiFactory
import retrofit2.HttpException

object AuthGuard {
    suspend fun isSessionValid(appConfig: AppConfig): Boolean {
        val repository = CameraRepository(ApiFactory.createAuthorized(appConfig))
        return runCatching { repository.getActiveSessions() }
            .fold(
                onSuccess = { true },
                onFailure = { ex ->
                    when (ex) {
                        is HttpException -> ex.code() != 401
                        else -> true
                    }
                },
            )
    }
}
