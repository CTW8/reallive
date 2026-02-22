package com.reallive.android.ui.watch

import android.content.Intent
import android.content.pm.ActivityInfo
import android.media.AudioManager
import android.os.Bundle
import android.util.Log
import android.view.View
import android.widget.TextView
import java.text.SimpleDateFormat
import androidx.appcompat.app.AppCompatActivity
import androidx.lifecycle.lifecycleScope
import androidx.recyclerview.widget.LinearLayoutManager
import androidx.recyclerview.widget.RecyclerView
import android.os.SystemClock
import com.reallive.android.R
import com.reallive.android.config.AppConfig
import com.reallive.android.data.CameraRepository
import com.reallive.android.network.ApiClient
import com.reallive.android.network.HistorySegmentDto
import com.reallive.android.network.HistoryThumbnailDto
import com.reallive.android.network.StreamInfoDto
import com.reallive.player.Player
import com.reallive.player.PlayerFactory
import com.reallive.player.PlayerSurfaceView
import com.reallive.android.ui.auth.LoginActivity
import com.reallive.android.ui.camera.CameraSettingsActivity
import com.reallive.android.ui.dashboard.DashboardActivity
import com.reallive.android.ui.history.HistoryActivity
import com.reallive.android.ui.notifications.NotificationsActivity
import com.reallive.android.ui.watch.ptz.PtzActivity
import com.reallive.android.ui.watch.snapshot.SnapshotGalleryActivity
import com.reallive.android.watch.WatchSessionManager
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import retrofit2.HttpException
import java.util.Date
import java.util.Locale

class WatchActivity : AppCompatActivity() {
    private lateinit var titleText: TextView
    private lateinit var statusText: TextView
    private lateinit var statusChip: TextView
    private lateinit var resolutionChip: TextView
    private lateinit var fpsChip: TextView
    private lateinit var bitrateChip: TextView
    private lateinit var liveBadgeText: TextView
    private lateinit var actionPlaybackText: TextView
    private lateinit var actionSnapshotText: TextView
    private lateinit var actionShareText: TextView
    private lateinit var actionAlertsText: TextView
    private lateinit var actionSettingsText: TextView
    private lateinit var actionPtzText: TextView
    private lateinit var actionDownloadText: TextView
    private lateinit var recentEventsTitleText: TextView
    private lateinit var playerView: PlayerSurfaceView
    private lateinit var placeholderIcon: View
    private lateinit var nightOverlay: View
    private lateinit var watermarkText: TextView
    private lateinit var eventAdapter: TimelineEventAdapter
    private lateinit var appConfig: AppConfig
    private lateinit var repository: CameraRepository
    private var player: Player? = null
    private var watchSession: WatchSessionManager? = null
    private var lastPlaybackTs: Long? = null
    private var micEnabled: Boolean = false
    private var isMuted: Boolean = false
    private var isFullscreen: Boolean = false
    private var streamFlv: String? = null
    private var streamHls: String? = null
    private var telemetryPollActive: Boolean = false
    private var telemetrySource: String = "NONE"
    private var telemetryPollCount: Int = 0
    private val telemetryPoller = Runnable { pollTelemetryTick() }

    private var cameraId: Long = -1
    private var cameraName: String = "Camera"
    private var streamKey: String = ""

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        appConfig = AppConfig(this)
        if (appConfig.getToken().isNullOrBlank()) {
            redirectToLogin()
            return
        }
        repository = CameraRepository(ApiClient.create(appConfig.getBaseUrl(), appConfig::getToken))
        setContentView(R.layout.activity_watch)

        cameraId = intent.getLongExtra(EXTRA_CAMERA_ID, 1L)
        cameraName = intent.getStringExtra(EXTRA_CAMERA_NAME)
            ?: if (isChineseLanguage(appConfig.getAppLanguage())) "前门" else "Front Door"
        streamKey = intent.getStringExtra(EXTRA_STREAM_KEY) ?: "front-door"

        titleText = findViewById(R.id.watch_title)
        statusText = findViewById(R.id.watch_status)
        statusChip = findViewById(R.id.watch_chip_status)
        resolutionChip = findViewById(R.id.watch_chip_resolution)
        fpsChip = findViewById(R.id.watch_chip_fps)
        bitrateChip = findViewById(R.id.watch_chip_bitrate)
        liveBadgeText = findViewById(R.id.watch_live_badge_text)
        actionPlaybackText = findViewById(R.id.watch_action_playback_label)
        actionSnapshotText = findViewById(R.id.watch_action_snapshot_label)
        actionShareText = findViewById(R.id.watch_action_share_label)
        actionAlertsText = findViewById(R.id.watch_action_alerts_label)
        actionSettingsText = findViewById(R.id.watch_action_settings_label)
        actionPtzText = findViewById(R.id.watch_action_ptz_label)
        actionDownloadText = findViewById(R.id.watch_action_download_label)
        recentEventsTitleText = findViewById(R.id.watch_recent_events_title)
        playerView = findViewById(R.id.watch_player_view)
        placeholderIcon = findViewById(R.id.watch_placeholder_icon)
        nightOverlay = findViewById(R.id.watch_night_overlay)
        watermarkText = findViewById(R.id.watch_watermark)

        titleText.text = cameraName
        statusChip.text = if (isChineseLanguage(appConfig.getAppLanguage())) "连接中" else "Connecting"
        applyLocalizedTexts(appConfig.getAppLanguage())

        setupActions()
        setupEventList()
        setupPlayer()
    }

    override fun onStart() {
        super.onStart()
        if (appConfig.shouldRequireReauth()) {
            redirectToLogin(forceReauth = true)
            return
        }
        appConfig.markAuthenticated()
        loadStream()
        loadTimeline()
        startTelemetryPolling()
    }

    override fun onStop() {
        super.onStop()
        stopTelemetryPolling()
        appConfig.markAppBackgrounded()
        lifecycleScope.launch {
            watchSession?.stop()
            watchSession = null
        }
    }

    override fun onDestroy() {
        super.onDestroy()
        playerView.unbindPlayer()
        player?.stop()
        player?.release()
        player = null
    }

    private fun setupActions() {
        findViewById<View>(R.id.btn_watch_back).setOnClickListener {
            navigateBackByDesign()
        }
        findViewById<View>(R.id.btn_watch_playback).setOnClickListener {
            startActivity(
                Intent(this, HistoryActivity::class.java).apply {
                    putExtra(WatchActivity.EXTRA_CAMERA_ID, cameraId)
                    putExtra(WatchActivity.EXTRA_CAMERA_NAME, cameraName)
                    putExtra(WatchActivity.EXTRA_STREAM_KEY, streamKey)
                },
            )
        }
        findViewById<View>(R.id.btn_watch_capture).setOnClickListener {
            startActivity(
                Intent(this, SnapshotGalleryActivity::class.java).apply {
                    putExtra(SnapshotGalleryActivity.EXTRA_CAMERA_ID, cameraId)
                    putExtra(SnapshotGalleryActivity.EXTRA_CAMERA_NAME, cameraName)
                },
            )
        }
        findViewById<View>(R.id.btn_watch_snapshot).setOnClickListener {
            startActivity(
                Intent(this, SnapshotGalleryActivity::class.java).apply {
                    putExtra(SnapshotGalleryActivity.EXTRA_CAMERA_ID, cameraId)
                    putExtra(SnapshotGalleryActivity.EXTRA_CAMERA_NAME, cameraName)
                },
            )
        }
        findViewById<View>(R.id.btn_watch_share).setOnClickListener {
            startActivity(
                Intent(this, ShareSheetActivity::class.java).apply {
                    putExtra(ShareSheetActivity.EXTRA_CAMERA_NAME, cameraName)
                    putExtra(ShareSheetActivity.EXTRA_STREAM_KEY, streamKey)
                    putExtra(ShareSheetActivity.EXTRA_CAMERA_ID, cameraId)
                },
            )
        }
        findViewById<View>(R.id.btn_watch_alerts).setOnClickListener {
            startActivity(Intent(this, NotificationsActivity::class.java))
        }
        findViewById<View>(R.id.btn_watch_settings).setOnClickListener {
            startActivity(
                Intent(this, CameraSettingsActivity::class.java).apply {
                    putExtra(CameraSettingsActivity.EXTRA_CAMERA_ID, cameraId)
                    putExtra(CameraSettingsActivity.EXTRA_CAMERA_NAME, cameraName)
                },
            )
        }
        findViewById<View>(R.id.btn_watch_ptz).setOnClickListener {
            startActivity(
                Intent(this, PtzActivity::class.java).apply {
                    putExtra(PtzActivity.EXTRA_CAMERA_ID, cameraId)
                    putExtra(PtzActivity.EXTRA_CAMERA_NAME, cameraName)
                },
            )
        }
        findViewById<View>(R.id.btn_watch_mic).setOnClickListener {
            micEnabled = !micEnabled
            updateMicButton()
        }
        findViewById<View>(R.id.btn_watch_volume).setOnClickListener {
            toggleMute()
        }
        findViewById<View>(R.id.btn_watch_quality).setOnClickListener {
            toggleQuality()
        }
        findViewById<View>(R.id.btn_watch_fullscreen).setOnClickListener {
            toggleFullscreen()
        }
        findViewById<View>(R.id.btn_watch_download).setOnClickListener {
            val ts = lastPlaybackTs ?: System.currentTimeMillis()
            startActivity(
                Intent(this, HistoryActivity::class.java).apply {
                    putExtra(WatchActivity.EXTRA_CAMERA_ID, cameraId)
                    putExtra(WatchActivity.EXTRA_CAMERA_NAME, cameraName)
                    putExtra(com.reallive.android.ui.watch.snapshot.SnapshotGalleryActivity.EXTRA_START_TS, ts)
                },
            )
        }
    }

    private fun setupEventList() {
        eventAdapter = TimelineEventAdapter { evt ->
            openEventDetail(evt)
        }
        findViewById<RecyclerView>(R.id.timeline_event_recycler).apply {
            layoutManager = LinearLayoutManager(this@WatchActivity)
            adapter = eventAdapter
        }
    }

    private fun setupPlayer() {
        player = PlayerFactory.create()
        player?.let { playerView.bindPlayer(it) }
        updateMicButton()
        updateVolumeButton()
    }

    private fun loadStream() {
        if (cameraId <= 0L) return
        lifecycleScope.launch {
            try {
                val info = withContext(Dispatchers.IO) { repository.getStreamInfo(cameraId) }
                applyStreamInfo(info)
                streamFlv = info.stream_urls?.pull_flv
                streamHls = info.stream_urls?.pull_hls
                val url = streamFlv ?: streamHls
                if (!url.isNullOrBlank()) {
                    placeholderIcon.visibility = View.GONE
                    player?.playLive(url)
                }
                watchSession = WatchSessionManager(ApiClient.create(appConfig.getBaseUrl(), appConfig::getToken), cameraId)
                watchSession?.start()
            } catch (ex: Exception) {
                if (ex is HttpException && ex.code() == 401) {
                    appConfig.clearAuth()
                    redirectToLogin()
                }
            }
        }
    }

    private fun applyStreamInfo(info: StreamInfoDto) {
        streamKey = info.stream_key
        val status = info.status.orEmpty()
        statusChip.text = when {
            status.equals("online", true) -> if (isChineseLanguage(appConfig.getAppLanguage())) "在线" else "Online"
            status.equals("connecting", true) -> if (isChineseLanguage(appConfig.getAppLanguage())) "连接中" else "Connecting"
            status.equals("offline", true) -> if (isChineseLanguage(appConfig.getAppLanguage())) "离线" else "Offline"
            status.isNotBlank() -> status.replaceFirstChar { it.titlecase(Locale.US) }
            else -> if (isChineseLanguage(appConfig.getAppLanguage())) "在线" else "Online"
        }
        val srsW = info.srs?.width ?: 0
        val srsH = info.srs?.height ?: 0
        resolutionChip.text = when {
            srsW > 0 && srsH > 0 -> "${srsW}x${srsH}"
            !info.camera?.resolution.isNullOrBlank() -> info.camera?.resolution ?: "--"
            else -> "--"
        }
        applyTelemetryChips(info)
        applyCameraRuntimeSettings(info)
    }

    private fun applyCameraRuntimeSettings(info: StreamInfoDto) {
        val cs = info.camera_settings
        if (cs == null) {
            playerView.scaleX = 1f
            playerView.scaleY = 1f
            playerView.rotation = 0f
            nightOverlay.visibility = View.GONE
            watermarkText.visibility = View.GONE
            return
        }
        when (cs.image_flip_mode.lowercase(Locale.US)) {
            "flip horizontal" -> {
                playerView.scaleX = -1f
                playerView.scaleY = 1f
                playerView.rotation = 0f
            }
            "flip vertical" -> {
                playerView.scaleX = 1f
                playerView.scaleY = -1f
                playerView.rotation = 0f
            }
            "rotate 180" -> {
                playerView.scaleX = 1f
                playerView.scaleY = 1f
                playerView.rotation = 180f
            }
            else -> {
                playerView.scaleX = 1f
                playerView.scaleY = 1f
                playerView.rotation = 0f
            }
        }
        // Night-vision color grading is done in pusher/player path. App overlay causes color cast.
        nightOverlay.visibility = View.GONE
        if (cs.watermark_enabled) {
            val ts = SimpleDateFormat("MM-dd HH:mm:ss", Locale.getDefault()).format(Date())
            watermarkText.text = "${cameraName}  $ts"
            watermarkText.visibility = View.VISIBLE
        } else {
            watermarkText.visibility = View.GONE
        }
    }

    private fun toggleMute() {
        val audio = getSystemService(AUDIO_SERVICE) as AudioManager
        isMuted = !isMuted
        if (android.os.Build.VERSION.SDK_INT >= 23) {
            audio.adjustStreamVolume(
                AudioManager.STREAM_MUSIC,
                if (isMuted) AudioManager.ADJUST_MUTE else AudioManager.ADJUST_UNMUTE,
                0,
            )
        } else {
            audio.setStreamMute(AudioManager.STREAM_MUSIC, isMuted)
        }
        updateVolumeButton()
    }

    private fun updateVolumeButton() {
        val icon = findViewById<android.widget.ImageView>(R.id.watch_volume_icon)
        icon.setImageResource(if (isMuted) R.drawable.ic_rl_volume_off_24 else R.drawable.ic_rl_volume_up_24)
    }

    private fun updateMicButton() {
        val icon = findViewById<android.widget.ImageView>(R.id.watch_mic_icon)
        icon.alpha = if (micEnabled) 1f else 0.6f
    }

    private fun toggleQuality() {
        val next = if (streamFlv != null && streamHls != null) {
            if ((playerView.tag as? String) == "hls") streamFlv else streamHls
        } else {
            streamFlv ?: streamHls
        }
        if (!next.isNullOrBlank()) {
            playerView.tag = if (next == streamHls) "hls" else "flv"
            player?.playLive(next)
        }
    }

    private fun toggleFullscreen() {
        isFullscreen = !isFullscreen
        requestedOrientation = if (isFullscreen) {
            ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE
        } else {
            ActivityInfo.SCREEN_ORIENTATION_PORTRAIT
        }
    }

    override fun onBackPressed() {
        if (isFullscreen) {
            isFullscreen = false
            requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_PORTRAIT
            return
        }
        navigateBackByDesign()
    }

    private fun loadTimeline() {
        if (cameraId <= 0L) return
        lifecycleScope.launch {
            try {
                val end = System.currentTimeMillis()
                val start = end - 2 * 60 * 60 * 1000L
                val timeline = withContext(Dispatchers.IO) {
                    repository.getTimeline(cameraId, start, end)
                }
                val items = timeline.events.map {
                    TimelineEventItem(
                        tsMs = it.ts,
                        type = it.type,
                        score = it.score ?: 0.0,
                        thumbnailUrl = resolveEventThumbnailUrl(it.ts, timeline.thumbnails, timeline.segments),
                    )
                }
                lastPlaybackTs = items.firstOrNull()?.tsMs
                eventAdapter.submitList(items)
            } catch (_: Exception) {
            }
        }
    }

    private fun resolveEventThumbnailUrl(
        ts: Long,
        thumbnails: List<HistoryThumbnailDto>,
        segments: List<HistorySegmentDto>,
    ): String? {
        val segment = findBestSegmentForTs(ts, segments)
        val inSegment = segment?.let { seg ->
            thumbnails.filter { it.ts >= seg.startMs && it.ts <= seg.endMs }
        }.orEmpty()
        val best = nearestEventThumbnail(ts, if (inSegment.isNotEmpty()) inSegment else thumbnails)
        if (best != null) return resolveMediaUrl(best.url)
        return segment?.thumbnailUrl?.takeIf { it.isNotBlank() }?.let { resolveMediaUrl(it) }
    }

    private fun findBestSegmentForTs(ts: Long, segments: List<HistorySegmentDto>): HistorySegmentDto? {
        if (segments.isEmpty()) return null
        val containing = segments.firstOrNull { ts >= it.startMs && ts <= it.endMs }
        if (containing != null) return containing
        return segments.minByOrNull { seg ->
            when {
                ts < seg.startMs -> seg.startMs - ts
                ts > seg.endMs -> ts - seg.endMs
                else -> 0L
            }
        }
    }

    private fun nearestEventThumbnail(ts: Long, thumbnails: List<HistoryThumbnailDto>): HistoryThumbnailDto? {
        if (thumbnails.isEmpty()) return null
        var best: HistoryThumbnailDto? = null
        var bestDist = Long.MAX_VALUE
        thumbnails.forEach { t ->
            val d = kotlin.math.abs(t.ts - ts)
            if (d < bestDist) {
                bestDist = d
                best = t
            }
        }
        return best
    }

    private fun resolveMediaUrl(raw: String): String {
        if (raw.startsWith("http://") || raw.startsWith("https://")) return raw
        val base = appConfig.getBaseUrl().removeSuffix("/")
        val path = if (raw.startsWith("/")) raw else "/$raw"
        return base + path
    }

    private fun openEventDetail(evt: TimelineEventItem) {
        startActivity(
            Intent(this, EventDetailActivity::class.java).apply {
                putExtra(EventDetailActivity.EXTRA_EVENT_TYPE, evt.type)
                putExtra(EventDetailActivity.EXTRA_EVENT_TS, evt.tsMs)
                putExtra(EventDetailActivity.EXTRA_EVENT_SCORE, evt.score)
                putExtra(EventDetailActivity.EXTRA_CAMERA_NAME, cameraName)
                putExtra(EventDetailActivity.EXTRA_CAMERA_ID, cameraId)
            },
        )
    }

    private fun redirectToLogin(forceReauth: Boolean = false) {
        startActivity(
            Intent(this, LoginActivity::class.java).apply {
                if (forceReauth) putExtra(LoginActivity.EXTRA_FORCE_REAUTH, true)
            },
        )
        finish()
    }

    private fun navigateBackByDesign() {
        if (isTaskRoot) {
            startActivity(
                Intent(this, DashboardActivity::class.java).apply {
                    addFlags(Intent.FLAG_ACTIVITY_CLEAR_TOP)
                    addFlags(Intent.FLAG_ACTIVITY_SINGLE_TOP)
                },
            )
        }
        finish()
    }

    private fun applyLocalizedTexts(languageCode: String) {
        val zh = isChineseLanguage(languageCode)
        liveBadgeText.text = if (zh) "直播中" else "LIVE"
        actionPlaybackText.text = if (zh) "回放" else "Playback"
        actionSnapshotText.text = if (zh) "截图" else "Snapshot"
        actionShareText.text = if (zh) "分享" else "Share"
        actionAlertsText.text = if (zh) "告警" else "Alerts"
        actionSettingsText.text = if (zh) "摄像头设置" else "Camera Config"
        actionPtzText.text = "PTZ"
        actionDownloadText.text = if (zh) "下载" else "Download"
        recentEventsTitleText.text = if (zh) "最近事件" else "Recent Events"
    }

    private fun isChineseLanguage(languageCode: String?): Boolean {
        if (languageCode.isNullOrBlank()) return false
        val normalized = languageCode.lowercase(Locale.US)
        return normalized.startsWith("zh")
    }

    private fun applyTelemetryChips(info: StreamInfoDto) {
        val seiLive = info.sei?.telemetry
        val seiHistory = info.sei?.telemetryHistory?.lastOrNull()
        val seiCfg = info.sei?.cameraConfig

        val seiFps = listOfNotNull(
            seiLive?.streamOutFps,
            seiHistory?.streamOutFps,
        ).firstOrNull { it > 0.0 }
        val seiBitrateKbps = listOfNotNull(
            seiLive?.streamOutBitrateKbps,
            seiLive?.streamOutBitrateBps?.let { it / 1000.0 },
            seiHistory?.streamOutBitrateKbps,
            seiHistory?.streamOutBitrateBps?.let { it / 1000.0 },
        ).firstOrNull { it > 0.0 }

        val srsFps = info.srs?.fps?.takeIf { it > 0.0 }
        val srsBitrateKbps = info.srs?.kbps?.recv_30s?.takeIf { it > 0L }?.toDouble()
            ?: info.srs?.kbps?.send_30s?.takeIf { it > 0L }?.toDouble()
        val seiCfgFps = seiCfg?.fps?.takeIf { it > 0.0 }
        val seiCfgBitrateKbps = seiCfg?.bitrate?.takeIf { it > 0L }?.let { it / 1000.0 }
        val effectiveFps = info.effectiveProfile?.targetFps?.takeIf { it > 0.0 }
        val effectiveBitrateKbps = info.effectiveProfile?.targetBitrateKbps?.takeIf { it > 0L }?.toDouble()

        val (fpsValue, kbpsValue, source) = if (seiFps != null || seiBitrateKbps != null) {
            Triple(seiFps, seiBitrateKbps, "SEI")
        } else if (seiCfgFps != null || seiCfgBitrateKbps != null) {
            Triple(seiCfgFps, seiCfgBitrateKbps, "SEI_CFG")
        } else if (effectiveFps != null || effectiveBitrateKbps != null) {
            Triple(effectiveFps, effectiveBitrateKbps, "EFFECTIVE")
        } else if (srsFps != null || srsBitrateKbps != null) {
            Triple(srsFps, srsBitrateKbps, "SRS")
        } else {
            Triple(null, null, "NONE")
        }

        fpsChip.text = if (fpsValue != null) String.format(Locale.US, "%.1ffps", fpsValue) else "--"
        bitrateChip.text = formatBitrate(kbpsValue?.toLong())

        telemetryPollCount += 1
        if (source != telemetrySource || telemetryPollCount % 10 == 0) {
            telemetrySource = source
            Log.i(
                "WatchTelemetry",
                    "source=$source fps=${fpsValue ?: -1.0} kbps=${kbpsValue?.toLong() ?: -1L} " +
                    "buildTag=${info.serverBuildTag ?: "null"} " +
                    "seiTs=${info.sei?.updatedAt ?: -1L} " +
                    "seiLiveFps=${seiLive?.streamOutFps ?: -1.0} " +
                    "seiLiveKbps=${seiLive?.streamOutBitrateKbps ?: -1.0} " +
                    "seiHistSize=${info.sei?.telemetryHistory?.size ?: 0} " +
                    "seiHistFps=${seiHistory?.streamOutFps ?: -1.0} " +
                    "seiHistKbps=${seiHistory?.streamOutBitrateKbps ?: -1.0} " +
                    "seiCfgFps=${seiCfg?.fps ?: -1.0} " +
                    "seiCfgBitrate=${seiCfg?.bitrate ?: -1L} " +
                    "effectiveMode=${info.effectiveProfile?.mode ?: "null"} " +
                    "effectiveProfile=${info.effectiveProfile?.profileOption ?: "null"} " +
                    "effectiveLevel=${info.effectiveProfile?.level ?: -1} " +
                    "effectiveFps=${info.effectiveProfile?.targetFps ?: -1.0} " +
                    "effectiveKbps=${info.effectiveProfile?.targetBitrateKbps ?: -1L} " +
                    "srsFps=${info.srs?.fps ?: -1.0} " +
                    "srsRecvKbps=${info.srs?.kbps?.recv_30s ?: -1L} " +
                    "srsSendKbps=${info.srs?.kbps?.send_30s ?: -1L}",
            )
        }
    }

    private fun formatBitrate(kbps: Long?): String {
        val value = kbps ?: return "--"
        return if (value >= 1000L) {
            String.format(Locale.US, "%.1fMbps", value / 1000.0)
        } else {
            "${value}kbps"
        }
    }

    private fun startTelemetryPolling() {
        if (telemetryPollActive) return
        telemetryPollActive = true
        bitrateChip.post(telemetryPoller)
    }

    private fun stopTelemetryPolling() {
        if (!telemetryPollActive) return
        telemetryPollActive = false
        bitrateChip.removeCallbacks(telemetryPoller)
    }

    private fun pollTelemetryTick() {
        if (!telemetryPollActive || cameraId <= 0L) return
        lifecycleScope.launch {
            try {
                val info = withContext(Dispatchers.IO) { repository.getStreamInfo(cameraId) }
                applyTelemetryChips(info)
            } catch (_: Exception) {
            } finally {
                if (telemetryPollActive) {
                    bitrateChip.postDelayed(telemetryPoller, 2000L)
                }
            }
        }
    }

    companion object {
        const val EXTRA_CAMERA_ID = "extra_camera_id"
        const val EXTRA_CAMERA_NAME = "extra_camera_name"
        const val EXTRA_STREAM_KEY = "extra_stream_key"
    }
}
