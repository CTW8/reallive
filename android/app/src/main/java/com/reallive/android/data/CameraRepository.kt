package com.reallive.android.data

import com.reallive.android.network.ActiveSessionResponse
import com.reallive.android.network.AuthLoginRequest
import com.reallive.android.network.AuthRegisterRequest
import com.reallive.android.network.AuthResponse
import com.reallive.android.network.CameraDto
import com.reallive.android.network.DashboardStatsDto
import com.reallive.android.network.HealthDto
import com.reallive.android.network.HistoryOverviewDto
import com.reallive.android.network.HistoryPlaybackDto
import com.reallive.android.network.HistoryTimelineDto
import com.reallive.android.network.RealLiveApi
import com.reallive.android.network.ReplayStopResponse
import com.reallive.android.network.SessionListResponse
import com.reallive.android.network.StreamInfoDto
import com.reallive.android.network.WatchHeartbeatResponse
import com.reallive.android.network.WatchStartResponse
import com.reallive.android.network.WatchStopResponse

class CameraRepository(private val api: RealLiveApi) {
    suspend fun login(username: String, password: String): AuthResponse {
        return api.login(AuthLoginRequest(username = username, password = password))
    }

    suspend fun register(username: String, email: String, password: String): AuthResponse {
        return api.register(AuthRegisterRequest(username = username, email = email, password = password))
    }

    suspend fun listCameras(): List<CameraDto> = api.listCameras()

    suspend fun createCamera(name: String, resolution: String): CameraDto {
        return api.createCamera(mapOf("name" to name, "resolution" to resolution))
    }

    suspend fun updateCamera(cameraId: Long, name: String, resolution: String): CameraDto {
        return api.updateCamera(cameraId, mapOf("name" to name, "resolution" to resolution))
    }

    suspend fun deleteCamera(cameraId: Long) {
        api.deleteCamera(cameraId)
    }

    suspend fun getStreamInfo(cameraId: Long): StreamInfoDto = api.getStreamInfo(cameraId)

    suspend fun getTimeline(cameraId: Long, startMs: Long, endMs: Long): HistoryTimelineDto {
        return api.getHistoryTimeline(cameraId, startMs, endMs)
    }

    suspend fun getHistoryOverview(cameraId: Long): HistoryOverviewDto {
        return api.getHistoryOverview(cameraId)
    }

    suspend fun getHistoryPlayback(cameraId: Long, tsMs: Long): HistoryPlaybackDto {
        return api.getHistoryPlayback(cameraId, tsMs)
    }

    suspend fun stopHistoryReplay(cameraId: Long, sessionId: String? = null): ReplayStopResponse {
        return api.stopHistoryReplay(cameraId, mapOf("sessionId" to sessionId))
    }

    suspend fun startWatch(cameraId: Long): WatchStartResponse = api.startWatch(cameraId)

    suspend fun heartbeatWatch(cameraId: Long, sessionId: String): WatchHeartbeatResponse {
        return api.heartbeatWatch(cameraId, mapOf("sessionId" to sessionId))
    }

    suspend fun stopWatch(cameraId: Long, sessionId: String): WatchStopResponse {
        return api.stopWatch(cameraId, mapOf("sessionId" to sessionId))
    }

    suspend fun getDashboardStats(): DashboardStatsDto = api.getDashboardStats()

    suspend fun getHealth(): HealthDto = api.getHealth()

    suspend fun getSessions(limit: Int = 20, offset: Int = 0): SessionListResponse {
        return api.listSessions(limit = limit, offset = offset)
    }

    suspend fun getActiveSessions(): ActiveSessionResponse = api.listActiveSessions()
}
