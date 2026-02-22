package com.reallive.android.network

import com.google.gson.annotations.SerializedName

data class AuthUserDto(
    val id: Long,
    val username: String,
    val email: String? = null,
)

data class AuthLoginRequest(
    val username: String,
    val password: String,
)

data class AuthRegisterRequest(
    val username: String,
    val email: String,
    val password: String,
)

data class AuthResponse(
    val token: String,
    val user: AuthUserDto,
)

data class ForgotPasswordRequest(
    val email: String,
)

data class ForgotPasswordResponse(
    val message: String,
    val temporaryPassword: String? = null,
)

data class CameraDto(
    val id: Long,
    val name: String,
    val stream_key: String,
    val created_at: String? = null,
    val resolution: String? = null,
    val status: String? = null,
    val thumbnailUrl: String? = null,
    val device: DeviceStateDto? = null,
    val stream_urls: StreamUrlsDto? = null,
)

data class CameraSettingsDetailDto(
    val camera_id: Long = 0L,
    val location: String = "",
    val motion_enabled: Boolean = true,
    val motion_sensitivity: String = "High",
    val person_enabled: Boolean = true,
    val sound_enabled: Boolean = false,
    val sound_sensitivity: String = "Loud",
    val detection_zones: String = "2 zones configured",
    val night_vision_enabled: Boolean = true,
    val night_vision_mode: String = "Auto",
    val image_flip_mode: String = "Normal",
    val watermark_enabled: Boolean = true,
    val stream_profile: String = "auto",
    val stream_mode: String = "auto",
    val manual_level: Int = 2,
    val auto_min_level: Int = 0,
    val auto_max_level: Int = 4,
    val auto_policy: String = "balanced",
    val auto_cooldown_sec: Int = 10,
    val auto_up_hold_sec: Int = 25,
    val auto_down_hold_sec: Int = 3,
    val firmware_version: String = "v2.3.8",
    val firmware_update_available: Boolean = true,
)

data class CameraSettingsResponse(
    val id: Long = 0L,
    val name: String = "",
    val resolution: String = "1080p",
    val location: String = "",
    val status: String = "offline",
    val settings: CameraSettingsDetailDto = CameraSettingsDetailDto(),
    val device: DeviceStateDto? = null,
    @SerializedName(value = "effective_profile", alternate = ["effectiveProfile"])
    val effectiveProfile: EffectiveProfileDto? = null,
)

data class UpdateCameraSettingsRequest(
    val name: String? = null,
    val resolution: String? = null,
    val location: String? = null,
    val settings: CameraSettingsDetailDto? = null,
)

data class CameraNetworkInfoDto(
    val cameraId: Long = 0L,
    val connected: Boolean = false,
    val ssid: String? = null,
    val signal: String? = null,
    val ip: String? = null,
    val model: String? = null,
)

data class FirmwareUpdateResponse(
    val ok: Boolean = false,
    val cameraId: Long = 0L,
    val firmwareVersion: String? = null,
    val firmwareUpdateAvailable: Boolean? = null,
)

data class DeviceStateDto(
    val ts: Long? = null,
    val running: Boolean? = null,
    val desiredLive: Boolean? = null,
    val activeLive: Boolean? = null,
    val reason: String? = null,
    val commandSeq: Long? = null,
    val updatedAt: Long? = null,
    val streamMode: String? = null,
    val streamProfile: String? = null,
    val profileLevel: Int? = null,
    val targetFps: Double? = null,
    val targetBitrateKbps: Long? = null,
    val autoPolicy: String? = null,
    val autoMinLevel: Int? = null,
    val autoMaxLevel: Int? = null,
    val ptzAction: String? = null,
    val ptzSpeed: Int? = null,
    val ptzZoomStep: Int? = null,
    val ptzZoomLevel: Int? = null,
    val ptzPreset: String? = null,
    val ptzUpdatedAt: Long? = null,
)

data class EffectiveProfileDto(
    val source: String? = null,
    val mode: String? = null,
    val profileOption: String? = null,
    val level: Int? = null,
    val targetFps: Double? = null,
    val targetBitrateKbps: Long? = null,
    val autoPolicy: String? = null,
    val autoMinLevel: Int? = null,
    val autoMaxLevel: Int? = null,
)

data class StreamInfoDto(
    @SerializedName(value = "serverBuildTag", alternate = ["server_build_tag"])
    val serverBuildTag: String? = null,
    val stream_key: String,
    val signaling_url: String? = null,
    val room: String? = null,
    val status: String? = null,
    val camera: CameraDto? = null,
    val stream_urls: StreamUrlsDto? = null,
    val srs: SrsStreamDto? = null,
    val sei: SeiInfoDto? = null,
    val camera_settings: CameraSettingsDetailDto? = null,
    val device: DeviceStateDto? = null,
    @SerializedName(value = "effective_profile", alternate = ["effectiveProfile"])
    val effectiveProfile: EffectiveProfileDto? = null,
    val liveDemand: WatchStartResponse? = null,
)

data class StreamUrlsDto(
    val push: String? = null,
    val pull_flv: String? = null,
    val pull_hls: String? = null,
)

data class SrsKbpsDto(
    val recv_30s: Long? = null,
    val send_30s: Long? = null,
)

data class SrsStreamDto(
    val codec: String? = null,
    val profile: String? = null,
    val width: Int? = null,
    val height: Int? = null,
    val fps: Double? = null,
    val clients: Int? = null,
    val kbps: SrsKbpsDto? = null,
)

data class SeiPersonDto(
    val active: Boolean? = null,
    val score: Double? = null,
    val ts: Long? = null,
    val bbox: HistoryEventBboxDto? = null,
)

data class SeiTelemetryDto(
    val cpuPct: Double? = null,
    val memoryPct: Double? = null,
    val storagePct: Double? = null,
    @SerializedName(value = "streamOutFps", alternate = ["stream_out_fps"])
    val streamOutFps: Double? = null,
    @SerializedName(value = "streamOutBitrateBps", alternate = ["stream_out_bitrate_bps"])
    val streamOutBitrateBps: Long? = null,
    @SerializedName(value = "streamOutBitrateKbps", alternate = ["stream_out_bitrate_kbps"])
    val streamOutBitrateKbps: Double? = null,
)

data class SeiCameraConfigDto(
    val fps: Double? = null,
    val bitrate: Long? = null,
)

data class SeiPtzDto(
    val simulated: Boolean? = null,
    val status: String? = null,
    val action: String? = null,
    val speed: Int? = null,
    @SerializedName(value = "zoomStep", alternate = ["zoom_step"])
    val zoomStep: Int? = null,
    @SerializedName(value = "zoomLevel", alternate = ["zoom_level"])
    val zoomLevel: Int? = null,
    val preset: String? = null,
    @SerializedName(value = "updatedAt", alternate = ["updated_at"])
    val updatedAt: Long? = null,
    @SerializedName(value = "panDeg", alternate = ["pan_deg"])
    val panDeg: Double? = null,
    @SerializedName(value = "tiltDeg", alternate = ["tilt_deg"])
    val tiltDeg: Double? = null,
    @SerializedName(value = "rollDeg", alternate = ["roll_deg"])
    val rollDeg: Double? = null,
)

data class SeiInfoDto(
    val updatedAt: Long? = null,
    val telemetry: SeiTelemetryDto? = null,
    val telemetryHistory: List<SeiTelemetryDto> = emptyList(),
    val cameraConfig: SeiCameraConfigDto? = null,
    val ptz: SeiPtzDto? = null,
    val person: SeiPersonDto? = null,
    val personEvents: List<HistoryTimelineEventDto> = emptyList(),
)

data class WatchStartResponse(
    val sessionId: String,
    val desiredLive: Boolean? = null,
    val appliedLive: Boolean? = null,
    val viewers: Int? = null,
)

data class WatchHeartbeatResponse(
    val ok: Boolean,
    val found: Boolean,
    val viewers: Int? = null,
)

data class WatchStopResponse(
    val ok: Boolean,
    val viewers: Int? = null,
)

data class PtzControlRequest(
    val action: String,
    val speed: Int? = null,
    val zoom_step: Int? = null,
    val zoom_level: Int? = null,
    val preset: String? = null,
)

data class PtzControlResponse(
    val ok: Boolean = false,
    val cameraId: Long? = null,
    val streamKey: String? = null,
    val action: String? = null,
    val speed: Int? = null,
    val zoom_step: Int? = null,
    val zoom_level: Int? = null,
    val preset: String? = null,
    val mqttEnabled: Boolean? = null,
    val mqttReady: Boolean? = null,
    val published: Boolean? = null,
    val device: DeviceStateDto? = null,
)

data class ReplayStopResponse(
    val ok: Boolean,
    val stopped: Boolean? = null,
    val reason: String? = null,
)

data class HistoryOverviewDto(
    val stream_key: String? = null,
    val source: String? = null,
    val hasHistory: Boolean,
    val nowMs: Long,
    val totalDurationMs: Long,
    val segmentCount: Int,
    val timeRange: HistoryTimeRangeDto? = null,
    val ranges: List<HistoryRangeDto> = emptyList(),
    val hasActiveRecording: Boolean? = null,
    val activeRecordingStartMs: Long? = null,
    val eventCount: Int? = null,
)

data class HistoryTimeRangeDto(
    val startMs: Long,
    val endMs: Long,
)

data class HistoryRangeDto(
    val startMs: Long,
    val endMs: Long,
)

data class HistoryThumbnailDto(
    val ts: Long,
    val url: String,
)

data class HistorySegmentDto(
    val id: String? = null,
    val startMs: Long,
    val endMs: Long,
    val durationMs: Long? = null,
    val url: String? = null,
    val thumbnailUrl: String? = null,
    val playable: Boolean? = null,
    val isOpen: Boolean? = null,
    val isActive: Boolean? = null,
)

data class HistoryEventBboxDto(
    val x: Int? = null,
    val y: Int? = null,
    val w: Int? = null,
    val h: Int? = null,
)

data class HistoryTimelineEventDto(
    val type: String,
    val ts: Long,
    val score: Double? = null,
    val bbox: HistoryEventBboxDto? = null,
)

data class HistoryTimelineDto(
    val stream_key: String? = null,
    val source: String? = null,
    val startMs: Long?,
    val endMs: Long?,
    val nowMs: Long? = null,
    val ranges: List<HistoryRangeDto> = emptyList(),
    val thumbnails: List<HistoryThumbnailDto> = emptyList(),
    val segments: List<HistorySegmentDto> = emptyList(),
    val events: List<HistoryTimelineEventDto> = emptyList(),
)

data class HistoryPlaybackSegmentDto(
    val id: String? = null,
    val startMs: Long? = null,
    val endMs: Long? = null,
    val durationMs: Long? = null,
)

data class HistoryPlaybackDto(
    val stream_key: String? = null,
    val source: String? = null,
    val mode: String,
    val requestedTs: Long,
    val playbackUrl: String?,
    val offsetSec: Long,
    val sessionId: String? = null,
    val transport: String? = null,
    val segment: HistoryPlaybackSegmentDto? = null,
)

data class DashboardCameraStatsDto(
    val total: Int = 0,
    val online: Int = 0,
    val streaming: Int = 0,
    val offline: Int = 0,
)

data class DashboardSessionStatsDto(
    val total: Int = 0,
    val active: Int = 0,
    val today: Int = 0,
)

data class DashboardSystemStatsDto(
    val uptime: Long = 0,
    val timestamp: String? = null,
)

data class DashboardStatsDto(
    val cameras: DashboardCameraStatsDto = DashboardCameraStatsDto(),
    val sessions: DashboardSessionStatsDto = DashboardSessionStatsDto(),
    val system: DashboardSystemStatsDto = DashboardSystemStatsDto(),
)

data class HealthDto(
    val status: String = "unknown",
    val timestamp: String? = null,
    val uptime: Long = 0,
    val memoryUsage: Long? = null,
    val nodeVersion: String? = null,
)

data class SessionDto(
    val id: Long,
    val camera_id: Long,
    val camera_name: String? = null,
    val start_time: String? = null,
    val end_time: String? = null,
    val status: String? = null,
    val duration_seconds: Long? = null,
)

data class AlertDto(
    val id: Long,
    val camera_id: Long? = null,
    val camera_name: String? = null,
    val type: String,
    val title: String,
    val description: String? = null,
    val status: String? = null,
    val created_at: String? = null,
    val event_ts_ms: Long? = null,
)

data class AlertListResponse(
    val items: List<AlertDto> = emptyList(),
    val total: Int = 0,
    val limit: Int = 0,
    val offset: Int = 0,
    val page: Int = 1,
    val total_pages: Int = 1,
)

data class AlertBatchRequest(
    val ids: List<Long>,
    val action: String,
)

data class AlertBatchResponse(
    val success: Boolean,
    val affected: Int,
)

data class UnreadCountDto(
    val count: Int = 0,
)

data class SessionListResponse(
    val sessions: List<SessionDto> = emptyList(),
    val total: Int = 0,
)

data class ActiveSessionResponse(
    val sessions: List<SessionDto> = emptyList(),
)

data class SessionRevokeResponse(
    val ok: Boolean = false,
)

data class DeleteCameraResponse(
    val message: String? = null,
)

data class SettingsProfileDto(
    val displayName: String = "",
    val email: String = "",
    val phone: String = "",
    val role: String = "",
    val signature: String = "",
    val language: String = "",
    val timezone: String = "",
)

data class SettingsNotificationsDto(
    val email: Boolean = true,
    val sms: Boolean = false,
    val webhook: Boolean = false,
    val sound: Boolean = false,
    val quietHours: String = "",
    val escalationDelay: String = "",
    val escalationRule: String = "",
)

data class SettingsSystemDto(
    val nvrMode: String = "",
    val networkProbe: Boolean = true,
    val autoLockSec: Int = 60,
    val darkMode: Boolean = false,
)

data class SettingsSecurityDto(
    val twoFactor: Boolean = false,
    val trustedDevice: Boolean = true,
    val ipAllowlist: Boolean = false,
)

data class SettingsAuditLogDto(
    val id: Long,
    val action: String = "",
    val type: String = "",
    val user: String = "",
    val time: String? = null,
)

data class SettingsResponse(
    val profile: SettingsProfileDto = SettingsProfileDto(),
    val notifications: SettingsNotificationsDto = SettingsNotificationsDto(),
    val system: SettingsSystemDto = SettingsSystemDto(),
    val security: SettingsSecurityDto = SettingsSecurityDto(),
    val updatedAt: String? = null,
    val auditLogs: List<SettingsAuditLogDto> = emptyList(),
)

data class DetectionProfileRequest(
    val detection: DetectionProfilePatch = DetectionProfilePatch(),
    val notifications: DetectionNotificationPatch = DetectionNotificationPatch(),
    val applyAllCameras: Boolean = true,
)

data class DetectionProfilePatch(
    val motionEnabled: Boolean = true,
    val personEnabled: Boolean = true,
    val soundEnabled: Boolean = false,
    val motionSensitivity: String = "High",
    val soundSensitivity: String = "Loud",
)

data class DetectionNotificationPatch(
    val pushEnabled: Boolean = true,
)

data class DetectionProfileStatus(
    val motionEnabled: Boolean = true,
    val personEnabled: Boolean = true,
    val soundEnabled: Boolean = false,
    val motionSensitivity: String = "High",
    val soundSensitivity: String = "Loud",
    val camerasUpdated: Int = 0,
    val applyAllCameras: Boolean = true,
)

data class DetectionProfileResponse(
    val profile: SettingsProfileDto = SettingsProfileDto(),
    val notifications: SettingsNotificationsDto = SettingsNotificationsDto(),
    val system: SettingsSystemDto = SettingsSystemDto(),
    val security: SettingsSecurityDto = SettingsSecurityDto(),
    val updatedAt: String? = null,
    val auditLogs: List<SettingsAuditLogDto> = emptyList(),
    val detection: DetectionProfileStatus = DetectionProfileStatus(),
)

data class UpdateProfileRequest(
    val displayName: String,
    val email: String,
    val phone: String = "",
    val signature: String = "",
    val language: String = "English",
    val timezone: String = "UTC+08:00",
)

data class UpdatePreferencesRequest(
    val notifications: SettingsNotificationsDto = SettingsNotificationsDto(),
    val system: SettingsSystemDto = SettingsSystemDto(),
    val security: SettingsSecurityDto = SettingsSecurityDto(),
)

data class ChangePasswordRequest(
    val currentPassword: String,
    val newPassword: String,
)

data class ChangePasswordResponse(
    val ok: Boolean = false,
)

data class SettingsAuditResponse(
    val rows: List<SettingsAuditLogDto> = emptyList(),
)

data class StorageCloudDto(
    val enabled: Boolean = false,
    val provider: String = "",
    val bucket: String = "",
    val region: String = "",
    val endpoint: String = "",
    val syncStatus: String = "",
    val lastSyncMs: Long = 0L,
    val totalGb: Double = 0.0,
    val usedGb: Double = 0.0,
    val freeGb: Double = 0.0,
    val usedPercent: Double = 0.0,
)

data class StorageOverviewDto(
    val total: Double = 0.0,
    val used: Double = 0.0,
    val free: Double = 0.0,
    val usedPercent: Double = 0.0,
    val deviceCount: Int = 0,
    val onlineCount: Int = 0,
    val cloud: StorageCloudDto = StorageCloudDto(),
)

data class StorageDeviceDto(
    val id: Long = 0L,
    val cameraId: Long = 0L,
    val streamKey: String = "",
    val name: String = "",
    val status: String = "",
    val totalGb: Double = 0.0,
    val usedGb: Double = 0.0,
    val freeGb: Double = 0.0,
    val usedPercent: Double = 0.0,
    val minFreePercent: Int = 15,
    val lastUpdateMs: Long = 0L,
)

data class StorageByDeviceResponse(
    val devices: List<StorageDeviceDto> = emptyList(),
)

data class StorageCloudConfigUpdateRequest(
    val enabled: Boolean,
    val provider: String,
    val bucket: String = "",
    val region: String = "",
    val endpoint: String = "",
    val totalGb: Double,
    val usedGb: Double,
)

data class StorageCloudConfigUpdateResponse(
    val success: Boolean = false,
    val cloud: StorageCloudDto = StorageCloudDto(),
)

data class StoragePlanDto(
    val id: String = "",
    val name: String = "",
    val totalGb: Double = 0.0,
    val priceMonthlyUsd: Double = 0.0,
    val description: String = "",
)

data class StoragePlansResponse(
    val plans: List<StoragePlanDto> = emptyList(),
    val currentTotalGb: Double = 0.0,
    val currentPlanId: String? = null,
)
