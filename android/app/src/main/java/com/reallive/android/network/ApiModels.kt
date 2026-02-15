package com.reallive.android.network

data class CameraDto(
    val id: Long,
    val name: String,
    val stream_key: String,
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
    val status: String? = null,
    val camera: CameraDto? = null,
    val device: DeviceStateDto? = null,
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
    val segment: HistoryPlaybackSegmentDto? = null,
)
