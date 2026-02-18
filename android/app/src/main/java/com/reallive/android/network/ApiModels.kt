package com.reallive.android.network

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

data class CameraDto(
    val id: Long,
    val name: String,
    val stream_key: String,
    val created_at: String? = null,
    val resolution: String? = null,
    val status: String? = null,
    val thumbnailUrl: String? = null,
    val device: DeviceStateDto? = null,
)

data class DeviceStateDto(
    val ts: Long? = null,
    val running: Boolean? = null,
    val desiredLive: Boolean? = null,
    val activeLive: Boolean? = null,
    val reason: String? = null,
    val commandSeq: Long? = null,
    val updatedAt: Long? = null,
)

data class StreamInfoDto(
    val stream_key: String,
    val signaling_url: String? = null,
    val room: String? = null,
    val status: String? = null,
    val camera: CameraDto? = null,
    val srs: SrsStreamDto? = null,
    val sei: SeiInfoDto? = null,
    val device: DeviceStateDto? = null,
    val liveDemand: WatchStartResponse? = null,
)

data class SrsKbpsDto(
    val recv_30s: Long? = null,
)

data class SrsStreamDto(
    val codec: String? = null,
    val profile: String? = null,
    val width: Int? = null,
    val height: Int? = null,
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
)

data class SeiInfoDto(
    val updatedAt: Long? = null,
    val telemetry: SeiTelemetryDto? = null,
    val telemetryHistory: List<SeiTelemetryDto> = emptyList(),
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

data class SessionListResponse(
    val sessions: List<SessionDto> = emptyList(),
    val total: Int = 0,
)

data class ActiveSessionResponse(
    val sessions: List<SessionDto> = emptyList(),
)

data class DeleteCameraResponse(
    val message: String? = null,
)
