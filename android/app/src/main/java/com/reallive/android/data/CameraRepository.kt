package com.reallive.android.data

import com.reallive.android.network.ActiveSessionResponse
import com.reallive.android.network.AlertBatchRequest
import com.reallive.android.network.AlertBatchResponse
import com.reallive.android.network.AlertDto
import com.reallive.android.network.AuthLoginRequest
import com.reallive.android.network.AuthRegisterRequest
import com.reallive.android.network.AuthResponse
import com.reallive.android.network.CameraDto
import com.reallive.android.network.CameraSettingsDetailDto
import com.reallive.android.network.CameraNetworkInfoDto
import com.reallive.android.network.CameraSettingsResponse
import com.reallive.android.network.DashboardStatsDto
import com.reallive.android.network.DetectionNotificationPatch
import com.reallive.android.network.DetectionProfilePatch
import com.reallive.android.network.DetectionProfileRequest
import com.reallive.android.network.DetectionProfileResponse
import com.reallive.android.network.ForgotPasswordResponse
import com.reallive.android.network.FirmwareUpdateResponse
import com.reallive.android.network.HealthDto
import com.reallive.android.network.HistoryOverviewDto
import com.reallive.android.network.HistoryPlaybackDto
import com.reallive.android.network.HistoryTimelineDto
import com.reallive.android.network.PtzControlRequest
import com.reallive.android.network.PtzControlResponse
import com.reallive.android.network.RealLiveApi
import com.reallive.android.network.ReplayStopResponse
import com.reallive.android.network.SettingsAuditLogDto
import com.reallive.android.network.SettingsSecurityDto
import com.reallive.android.network.SettingsSystemDto
import com.reallive.android.network.SettingsResponse
import com.reallive.android.network.StorageDeviceDto
import com.reallive.android.network.StorageCloudDto
import com.reallive.android.network.StorageCloudConfigUpdateRequest
import com.reallive.android.network.StoragePlansResponse
import com.reallive.android.network.StorageOverviewDto
import com.reallive.android.network.SessionListResponse
import com.reallive.android.network.SessionRevokeResponse
import com.reallive.android.network.StreamInfoDto
import com.reallive.android.network.SettingsNotificationsDto
import com.reallive.android.network.UpdatePreferencesRequest
import com.reallive.android.network.UpdateProfileRequest
import com.reallive.android.network.UpdateCameraSettingsRequest
import com.reallive.android.network.WatchHeartbeatResponse
import com.reallive.android.network.WatchStartResponse
import com.reallive.android.network.WatchStopResponse
import com.reallive.android.network.UnreadCountDto
import com.reallive.android.network.ChangePasswordResponse
import com.reallive.android.network.ChangePasswordRequest

class CameraRepository(private val api: RealLiveApi) {
    suspend fun login(username: String, password: String): AuthResponse {
        return api.login(AuthLoginRequest(username = username, password = password))
    }

    suspend fun register(username: String, email: String, password: String): AuthResponse {
        return api.register(AuthRegisterRequest(username = username, email = email, password = password))
    }

    suspend fun forgotPassword(email: String): ForgotPasswordResponse {
        return api.forgotPassword(com.reallive.android.network.ForgotPasswordRequest(email = email))
    }

    suspend fun listCameras(): List<CameraDto> = api.listCameras()

    suspend fun createCamera(name: String, resolution: String): CameraDto {
        return api.createCamera(mapOf("name" to name, "resolution" to resolution))
    }

    suspend fun updateCamera(cameraId: Long, name: String, resolution: String): CameraDto {
        return api.updateCamera(cameraId, mapOf("name" to name, "resolution" to resolution))
    }

    suspend fun getCameraSettings(cameraId: Long): CameraSettingsResponse {
        return api.getCameraSettings(cameraId)
    }

    suspend fun updateCameraSettings(
        cameraId: Long,
        name: String? = null,
        resolution: String? = null,
        location: String? = null,
        settings: CameraSettingsDetailDto? = null,
    ): CameraSettingsResponse {
        return api.updateCameraSettings(
            cameraId,
            UpdateCameraSettingsRequest(
                name = name,
                resolution = resolution,
                location = location,
                settings = settings,
            ),
        )
    }

    suspend fun deleteCamera(cameraId: Long) {
        api.deleteCamera(cameraId)
    }

    suspend fun getCameraNetworkInfo(cameraId: Long): CameraNetworkInfoDto {
        return api.getCameraNetworkInfo(cameraId)
    }

    suspend fun triggerFirmwareUpdate(cameraId: Long): FirmwareUpdateResponse {
        return api.triggerFirmwareUpdate(cameraId)
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

    suspend fun sendPtzCommand(
        cameraId: Long,
        action: String,
        speed: Int? = null,
        zoomStep: Int? = null,
        zoomLevel: Int? = null,
        preset: String? = null,
    ): PtzControlResponse {
        return api.sendPtzCommand(
            cameraId,
            PtzControlRequest(
                action = action,
                speed = speed,
                zoom_step = zoomStep,
                zoom_level = zoomLevel,
                preset = preset,
            ),
        )
    }

    suspend fun getDashboardStats(): DashboardStatsDto = api.getDashboardStats()

    suspend fun getHealth(): HealthDto = api.getHealth()

    suspend fun getSessions(limit: Int = 20, offset: Int = 0): SessionListResponse {
        return api.listSessions(limit = limit, offset = offset)
    }

    suspend fun getActiveSessions(): ActiveSessionResponse = api.listActiveSessions()

    suspend fun revokeSession(sessionId: Long): SessionRevokeResponse = api.revokeSession(sessionId)

    suspend fun getAlerts(
        limit: Int? = 50,
        offset: Int? = 0,
        typeGroup: String? = null,
        status: String? = null,
        query: String? = null,
        since: String? = null,
        until: String? = null,
    ): List<AlertDto> {
        return api.listAlertsPaged(
            limit = limit,
            offset = offset,
            paged = 1,
            typeGroup = typeGroup,
            status = status,
            query = query,
            since = since,
            until = until,
        ).items
    }

    suspend fun markAlertsRead(ids: List<Long>): AlertBatchResponse {
        return api.updateAlerts(AlertBatchRequest(ids = ids, action = "read"))
    }

    suspend fun markAlertRead(alertId: Long): AlertDto {
        return api.markAlertRead(alertId)
    }

    suspend fun getUnreadCount(): UnreadCountDto {
        return api.getUnreadCount()
    }

    suspend fun getSettings(): SettingsResponse {
        return api.getSettings()
    }

    suspend fun updateProfile(
        displayName: String,
        email: String,
        phone: String,
        signature: String,
        language: String,
        timezone: String,
    ): SettingsResponse {
        return api.updateProfile(
            UpdateProfileRequest(
                displayName = displayName,
                email = email,
                phone = phone,
                signature = signature,
                language = language,
                timezone = timezone,
            ),
        )
    }

    suspend fun updatePreferences(
        notifications: SettingsNotificationsDto,
        system: SettingsSystemDto,
        security: SettingsSecurityDto,
    ): SettingsResponse {
        return api.updatePreferences(
            UpdatePreferencesRequest(
                notifications = notifications,
                system = system,
                security = security,
            ),
        )
    }

    suspend fun updateDetectionProfile(
        pushEnabled: Boolean,
        motionEnabled: Boolean,
        personEnabled: Boolean,
        soundEnabled: Boolean,
        motionSensitivity: String,
        soundSensitivity: String,
        applyAllCameras: Boolean = true,
    ): DetectionProfileResponse {
        return api.updateDetectionProfile(
            DetectionProfileRequest(
                detection = DetectionProfilePatch(
                    motionEnabled = motionEnabled,
                    personEnabled = personEnabled,
                    soundEnabled = soundEnabled,
                    motionSensitivity = motionSensitivity,
                    soundSensitivity = soundSensitivity,
                ),
                notifications = DetectionNotificationPatch(
                    pushEnabled = pushEnabled,
                ),
                applyAllCameras = applyAllCameras,
            ),
        )
    }

    suspend fun changePassword(currentPassword: String, newPassword: String): ChangePasswordResponse {
        return api.changePassword(
            ChangePasswordRequest(
                currentPassword = currentPassword,
                newPassword = newPassword,
            ),
        )
    }

    suspend fun getSettingsAudit(limit: Int = 40): List<SettingsAuditLogDto> {
        return api.getSettingsAudit(limit).rows
    }

    suspend fun getStorageOverview(): StorageOverviewDto {
        return api.getStorageOverview()
    }

    suspend fun getStorageByDevice(): List<StorageDeviceDto> {
        return api.getStorageByDevice().devices
    }

    suspend fun getStorageCloudConfig(): StorageCloudDto {
        return api.getStorageCloudConfig()
    }

    suspend fun updateStorageCloudConfig(
        enabled: Boolean,
        provider: String,
        bucket: String,
        region: String,
        endpoint: String,
        totalGb: Double,
        usedGb: Double,
    ): StorageCloudDto {
        return api.updateStorageCloudConfig(
            StorageCloudConfigUpdateRequest(
                enabled = enabled,
                provider = provider,
                bucket = bucket,
                region = region,
                endpoint = endpoint,
                totalGb = totalGb,
                usedGb = usedGb,
            ),
        ).cloud
    }

    suspend fun getStoragePlans(): StoragePlansResponse {
        return api.getStoragePlans()
    }
}
