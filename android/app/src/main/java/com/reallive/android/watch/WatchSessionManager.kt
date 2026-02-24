package com.reallive.android.watch

import com.reallive.android.network.RealLiveApi
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import retrofit2.HttpException

class WatchSessionManager(
    private val api: RealLiveApi,
    private val cameraId: Long,
    private val onUnauthorized: (() -> Unit)? = null,
) {
    private val scope = CoroutineScope(Dispatchers.IO)
    private var heartbeatJob: Job? = null
    private var sessionId: String? = null

    suspend fun start() {
        if (sessionId == null) {
            val response = api.startWatch(cameraId)
            sessionId = response.sessionId
        }
        if (heartbeatJob?.isActive == true) return
        heartbeatJob = scope.launch {
            while (isActive) {
                try {
                    val sid = sessionId ?: run {
                        val response = api.startWatch(cameraId)
                        sessionId = response.sessionId
                        response.sessionId
                    }
                    api.heartbeatWatch(cameraId, mapOf("sessionId" to sid))
                    delay(10_000)
                } catch (ce: CancellationException) {
                    throw ce
                } catch (ex: HttpException) {
                    if (ex.code() == 401) {
                        sessionId = null
                        onUnauthorized?.invoke()
                        break
                    }
                    sessionId = null
                    delay(2_000)
                } catch (_: Exception) {
                    sessionId = null
                    delay(2_000)
                }
            }
        }
    }

    suspend fun stop() {
        val sid = sessionId
        heartbeatJob?.cancel()
        heartbeatJob = null
        sessionId = null
        if (!sid.isNullOrBlank()) {
            runCatching {
                api.stopWatch(cameraId, mapOf("sessionId" to sid))
            }
        }
    }
}
