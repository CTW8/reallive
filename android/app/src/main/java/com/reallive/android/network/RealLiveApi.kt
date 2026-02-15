package com.reallive.android.network

import retrofit2.http.Body
import retrofit2.http.GET
import retrofit2.http.POST
import retrofit2.http.Path
import retrofit2.http.Query

interface RealLiveApi {
    @GET("api/cameras")
    suspend fun listCameras(): List<CameraDto>

    @GET("api/cameras/{id}/stream")
    suspend fun getStreamInfo(@Path("id") cameraId: Long): StreamInfoDto

    @POST("api/cameras/{id}/watch/start")
    suspend fun startWatch(@Path("id") cameraId: Long): WatchStartResponse

    @POST("api/cameras/{id}/watch/heartbeat")
    suspend fun heartbeatWatch(
        @Path("id") cameraId: Long,
        @Body body: Map<String, String>,
    ): WatchHeartbeatResponse

    @POST("api/cameras/{id}/watch/stop")
    suspend fun stopWatch(
        @Path("id") cameraId: Long,
        @Body body: Map<String, String>,
    ): WatchStopResponse

    @GET("api/cameras/{id}/history/overview")
    suspend fun getHistoryOverview(@Path("id") cameraId: Long): HistoryOverviewDto

    @GET("api/cameras/{id}/history/timeline")
    suspend fun getHistoryTimeline(
        @Path("id") cameraId: Long,
        @Query("start") startMs: Long,
        @Query("end") endMs: Long,
    ): HistoryTimelineDto

    @GET("api/cameras/{id}/history/play")
    suspend fun getHistoryPlayback(
        @Path("id") cameraId: Long,
        @Query("ts") tsMs: Long,
    ): HistoryPlaybackDto
}
