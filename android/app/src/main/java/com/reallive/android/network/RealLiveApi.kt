package com.reallive.android.network

import retrofit2.http.Body
import retrofit2.http.DELETE
import retrofit2.http.GET
import retrofit2.http.POST
import retrofit2.http.PUT
import retrofit2.http.Path
import retrofit2.http.Query

interface RealLiveApi {
    @POST("api/auth/login")
    suspend fun login(@Body body: AuthLoginRequest): AuthResponse

    @POST("api/auth/register")
    suspend fun register(@Body body: AuthRegisterRequest): AuthResponse

    @GET("api/cameras")
    suspend fun listCameras(): List<CameraDto>

    @POST("api/cameras")
    suspend fun createCamera(@Body body: Map<String, String>): CameraDto

    @PUT("api/cameras/{id}")
    suspend fun updateCamera(
        @Path("id") cameraId: Long,
        @Body body: Map<String, String>,
    ): CameraDto

    @DELETE("api/cameras/{id}")
    suspend fun deleteCamera(@Path("id") cameraId: Long): DeleteCameraResponse

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

    @POST("api/cameras/{id}/history/replay/stop")
    suspend fun stopHistoryReplay(
        @Path("id") cameraId: Long,
        @Body body: Map<String, String?>,
    ): ReplayStopResponse

    @GET("api/dashboard/stats")
    suspend fun getDashboardStats(): DashboardStatsDto

    @GET("api/health")
    suspend fun getHealth(): HealthDto

    @GET("api/sessions")
    suspend fun listSessions(
        @Query("limit") limit: Int = 20,
        @Query("offset") offset: Int = 0,
    ): SessionListResponse

    @GET("api/sessions/active")
    suspend fun listActiveSessions(): ActiveSessionResponse
}
