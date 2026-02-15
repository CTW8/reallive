package com.reallive.android.data

import com.reallive.android.network.CameraDto
import com.reallive.android.network.HistoryOverviewDto
import com.reallive.android.network.HistoryPlaybackDto
import com.reallive.android.network.HistoryTimelineDto
import com.reallive.android.network.RealLiveApi
import com.reallive.android.network.StreamInfoDto
import com.reallive.android.network.WatchHeartbeatResponse
import com.reallive.android.network.WatchStartResponse
import com.reallive.android.network.WatchStopResponse

class CameraRepository(private val api: RealLiveApi) {
    suspend fun listCameras(): List<CameraDto> = api.listCameras()

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

    suspend fun startWatch(cameraId: Long): WatchStartResponse = api.startWatch(cameraId)

    suspend fun heartbeatWatch(cameraId: Long, sessionId: String): WatchHeartbeatResponse {
        return api.heartbeatWatch(cameraId, mapOf("sessionId" to sessionId))
    }

    suspend fun stopWatch(cameraId: Long, sessionId: String): WatchStopResponse {
        return api.stopWatch(cameraId, mapOf("sessionId" to sessionId))
    }
}
