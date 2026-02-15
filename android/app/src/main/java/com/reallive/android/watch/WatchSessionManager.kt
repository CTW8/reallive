package com.reallive.android.watch

import com.reallive.android.network.RealLiveApi
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch

class WatchSessionManager(
    private val api: RealLiveApi,
    private val cameraId: Long,
) {
    private val scope = CoroutineScope(Dispatchers.IO)
    private var heartbeatJob: Job? = null
    private var sessionId: String? = null

    suspend fun start() {
        if (sessionId != null) return
        val response = api.startWatch(cameraId)
        sessionId = response.sessionId
        heartbeatJob = scope.launch {
            while (isActive) {
                val sid = sessionId ?: break
                api.heartbeatWatch(cameraId, mapOf("sessionId" to sid))
                delay(10_000)
            }
        }
    }

    suspend fun stop() {
        val sid = sessionId
        heartbeatJob?.cancel()
        heartbeatJob = null
        sessionId = null
        if (!sid.isNullOrBlank()) {
            api.stopWatch(cameraId, mapOf("sessionId" to sid))
        }
    }
}
