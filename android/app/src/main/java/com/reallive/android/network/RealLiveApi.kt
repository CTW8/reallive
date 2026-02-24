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

    @POST("api/auth/forgot-password")
    suspend fun forgotPassword(@Body body: ForgotPasswordRequest): ForgotPasswordResponse

    @POST("api/auth/refresh")
    suspend fun refreshToken(@Body body: RefreshTokenRequest): AuthResponse

    @POST("api/auth/logout")
    suspend fun logout(): SessionRevokeResponse

    @DELETE("api/auth/me")
    suspend fun deleteMe(): SessionRevokeResponse

    @GET("api/cameras")
    suspend fun listCameras(): List<CameraDto>

    @POST("api/cameras")
    suspend fun createCamera(@Body body: Map<String, String>): CameraDto

    @PUT("api/cameras/{id}")
    suspend fun updateCamera(
        @Path("id") cameraId: Long,
        @Body body: Map<String, String>,
    ): CameraDto

    @GET("api/cameras/{id}/settings")
    suspend fun getCameraSettings(@Path("id") cameraId: Long): CameraSettingsResponse

    @PUT("api/cameras/{id}/settings")
    suspend fun updateCameraSettings(
        @Path("id") cameraId: Long,
        @Body body: UpdateCameraSettingsRequest,
    ): CameraSettingsResponse

    @GET("api/cameras/{id}/network")
    suspend fun getCameraNetworkInfo(@Path("id") cameraId: Long): CameraNetworkInfoDto

    @POST("api/cameras/{id}/firmware/update")
    suspend fun triggerFirmwareUpdate(@Path("id") cameraId: Long): FirmwareUpdateResponse

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

    @POST("api/cameras/{id}/ptz")
    suspend fun sendPtzCommand(
        @Path("id") cameraId: Long,
        @Body body: PtzControlRequest,
    ): PtzControlResponse

    @POST("api/cameras/{id}/share-link")
    suspend fun createShareLink(
        @Path("id") cameraId: Long,
        @Body body: Map<String, @JvmSuppressWildcards Any>,
    ): ShareLinkResponse

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

    @POST("api/sessions/{id}/revoke")
    suspend fun revokeSession(@Path("id") sessionId: Long): SessionRevokeResponse

    @GET("api/auth/sessions")
    suspend fun listAuthSessions(): AuthSessionListResponse

    @POST("api/auth/sessions/{id}/revoke")
    suspend fun revokeAuthSession(@Path("id") sessionId: Long): SessionRevokeResponse

    @POST("api/auth/sessions/revoke-others")
    suspend fun revokeOtherAuthSessions(): SessionRevokeResponse

    @GET("api/alerts")
    suspend fun listAlertsPaged(
        @Query("limit") limit: Int? = null,
        @Query("offset") offset: Int? = null,
        @Query("paged") paged: Int? = 1,
        @Query("type_group") typeGroup: String? = null,
        @Query("status") status: String? = null,
        @Query("q") query: String? = null,
        @Query("since") since: String? = null,
        @Query("until") until: String? = null,
    ): AlertListResponse

    @POST("api/alerts/batch")
    suspend fun updateAlerts(@Body body: AlertBatchRequest): AlertBatchResponse

    @POST("api/alerts/{id}/read")
    suspend fun markAlertRead(@Path("id") alertId: Long): AlertDto

    @GET("api/alerts/unread-count")
    suspend fun getUnreadCount(): UnreadCountDto

    @GET("api/settings")
    suspend fun getSettings(): SettingsResponse

    @PUT("api/settings/profile")
    suspend fun updateProfile(@Body body: UpdateProfileRequest): SettingsResponse

    @PUT("api/settings/preferences")
    suspend fun updatePreferences(@Body body: UpdatePreferencesRequest): SettingsResponse

    @POST("api/settings/detection-profile")
    suspend fun updateDetectionProfile(@Body body: DetectionProfileRequest): DetectionProfileResponse

    @POST("api/settings/password")
    suspend fun changePassword(@Body body: ChangePasswordRequest): ChangePasswordResponse

    @GET("api/settings/audit")
    suspend fun getSettingsAudit(
        @Query("limit") limit: Int = 40,
    ): SettingsAuditResponse

    @GET("api/storage/overview")
    suspend fun getStorageOverview(): StorageOverviewDto

    @GET("api/storage/by-device")
    suspend fun getStorageByDevice(): StorageByDeviceResponse

    @GET("api/storage/cloud-config")
    suspend fun getStorageCloudConfig(): StorageCloudDto

    @POST("api/storage/cloud-config")
    suspend fun updateStorageCloudConfig(
        @Body body: StorageCloudConfigUpdateRequest,
    ): StorageCloudConfigUpdateResponse

    @GET("api/storage/plans")
    suspend fun getStoragePlans(): StoragePlansResponse
}
