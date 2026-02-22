package com.reallive.android.ui.watch

import android.content.Intent
import android.net.Uri
import android.os.Bundle
import android.os.SystemClock
import android.view.View
import android.widget.ImageButton
import android.widget.SeekBar
import android.widget.TextView
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import androidx.lifecycle.lifecycleScope
import com.reallive.android.R
import com.reallive.android.config.AppConfig
import com.reallive.android.data.CameraRepository
import com.reallive.android.network.ApiClient
import com.reallive.player.Player
import com.reallive.player.PlayerFactory
import com.reallive.player.PlayerSurfaceView
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import retrofit2.HttpException
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

class EventDetailActivity : AppCompatActivity() {
    private lateinit var appConfig: AppConfig
    private lateinit var repository: CameraRepository
    private lateinit var playerView: PlayerSurfaceView
    private var player: Player? = null
    private var playbackSessionId: String? = null
    private var playbackUrl: String? = null
    private var eventTs: Long = 0L
    private var cameraId: Long = -1L
    private var segmentStartMs: Long = -1L
    private var segmentDurationMs: Long = 0L
    private var currentTsMs: Long = -1L
    private var isPaused: Boolean = false
    private var isUserSeeking: Boolean = false
    private var isProgressTickerActive: Boolean = false
    private var lastTickAtUptimeMs: Long = 0L
    private var pauseAtOffsetMs: Long = 0L
    private lateinit var pageTitleText: TextView
    private lateinit var subtitleText: TextView
    private lateinit var badgeTypeText: TextView
    private lateinit var badgePriorityText: TextView
    private lateinit var zoneTitleText: TextView
    private lateinit var confidenceTitleText: TextView
    private lateinit var storageTitleText: TextView
    private lateinit var downloadText: TextView
    private lateinit var shareText: TextView
    private lateinit var nightOverlay: View
    private lateinit var watermarkText: TextView
    private lateinit var seekBar: SeekBar
    private lateinit var currentTimeText: TextView
    private lateinit var totalTimeText: TextView
    private lateinit var playPauseButton: ImageButton

    private val progressTicker = object : Runnable {
        override fun run() {
            if (!isProgressTickerActive) return
            val now = SystemClock.uptimeMillis()
            if (lastTickAtUptimeMs == 0L) lastTickAtUptimeMs = now
            if (!isPaused && !isUserSeeking && segmentStartMs > 0L && segmentDurationMs > 0L) {
                val stats = player?.getStats()
                val nativePosMs = stats?.currentPositionMs ?: -1L
                if (nativePosMs >= 0L) {
                    currentTsMs = (segmentStartMs + nativePosMs).coerceIn(segmentStartMs, segmentStartMs + segmentDurationMs)
                    updateProgressUi(currentTsMs)
                }
            }
            lastTickAtUptimeMs = now
            seekBar.postDelayed(this, 200L)
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_event_detail)
        findViewById<android.view.View>(R.id.event_detail_back).setOnClickListener { finish() }

        appConfig = AppConfig(this)
        repository = CameraRepository(ApiClient.create(appConfig.getBaseUrl(), appConfig::getToken))
        val type = intent.getStringExtra(EXTRA_EVENT_TYPE) ?: "event"
        eventTs = intent.getLongExtra(EXTRA_EVENT_TS, System.currentTimeMillis())
        val score = intent.getDoubleExtra(EXTRA_EVENT_SCORE, 0.0)
        val zh = isChineseLanguage(appConfig.getAppLanguage())
        val cameraName = intent.getStringExtra(EXTRA_CAMERA_NAME) ?: if (zh) "摄像头" else "Camera"
        cameraId = intent.getLongExtra(EXTRA_CAMERA_ID, -1L)
        pageTitleText = findViewById(R.id.event_detail_page_title)
        subtitleText = findViewById(R.id.event_detail_subtitle)
        badgeTypeText = findViewById(R.id.event_detail_badge_type)
        badgePriorityText = findViewById(R.id.event_detail_badge_priority)
        zoneTitleText = findViewById(R.id.event_detail_zone_title)
        confidenceTitleText = findViewById(R.id.event_detail_confidence_title)
        storageTitleText = findViewById(R.id.event_detail_storage_title)
        downloadText = findViewById(R.id.event_detail_download)
        shareText = findViewById(R.id.event_detail_share)
        playerView = findViewById(R.id.event_detail_player_view)
        nightOverlay = findViewById(R.id.event_detail_night_overlay)
        watermarkText = findViewById(R.id.event_detail_watermark)
        seekBar = findViewById(R.id.event_detail_seekbar)
        currentTimeText = findViewById(R.id.event_detail_progress_current)
        totalTimeText = findViewById(R.id.event_detail_progress_total)
        playPauseButton = findViewById(R.id.event_detail_play_pause)
        player = PlayerFactory.create()
        player?.let { playerView.bindPlayer(it) }
        applyLocalizedTexts(zh, type)
        subtitleText.text = cameraName
        bindPlaybackControls()
        findViewById<View>(R.id.event_detail_play_overlay).setOnClickListener {
            if (eventTs > 0L) {
                isPaused = false
                updatePlayPauseUi()
                loadPlayback(eventTs)
            }
        }

        val time = SimpleDateFormat(
            "MMM dd, yyyy · hh:mm:ss a",
            localeForLanguage(appConfig.getAppLanguage()),
        ).format(Date(eventTs))
        findViewById<TextView>(R.id.event_detail_title).text = when (type.lowercase(Locale.getDefault())) {
            "person-detected", "person" -> if (zh) "$cameraName 发现人物" else "Person Detected at $cameraName"
            "motion" -> if (zh) "$cameraName 检测到移动" else "Motion Detected at $cameraName"
            else -> type.replace('-', ' ').replaceFirstChar { it.uppercase() }
        }
        findViewById<TextView>(R.id.event_detail_meta).text =
            if (zh) "$time · 时长：--" else "$time · Duration: --"
        findViewById<TextView>(R.id.event_detail_score).text = if (score > 0.0) {
            if (zh) "人物: ${(score * 100.0).toInt()}% · 人脸: 未识别" else "Person: ${(score * 100.0).toInt()}% · Face: Not recognized"
        } else {
            if (zh) "人物: - · 人脸: 未识别" else "Person: - · Face: Not recognized"
        }
        findViewById<TextView>(R.id.event_detail_camera_name).text = cameraName
        findViewById<TextView>(R.id.event_detail_zone).text = if (type.lowercase(Locale.US).contains("person")) {
            if (zh) "区域 A - 入口区域" else "Zone A - Entrance Area"
        } else {
            if (zh) "区域 B - 移动区域" else "Zone B - Motion Area"
        }

        loadCameraInfo()
        loadPlayback(eventTs)

        findViewById<android.view.View>(R.id.event_detail_download).setOnClickListener {
            if (cameraId <= 0L) return@setOnClickListener
            lifecycleScope.launch {
                try {
                val playback = withContext(Dispatchers.IO) {
                    repository.getHistoryPlayback(cameraId, eventTs)
                }
                val url = playback.playbackUrl
                if (!url.isNullOrBlank()) {
                    startActivity(Intent(Intent.ACTION_VIEW, Uri.parse(resolveMediaUrl(url))))
                }
            } catch (ex: Exception) {
                if (ex is HttpException && ex.code() == 401) {
                    appConfig.clearAuth()
                        finish()
                    }
                }
            }
        }
        findViewById<android.view.View>(R.id.event_detail_share).setOnClickListener {
            startActivity(
                Intent(this, ShareSheetActivity::class.java).apply {
                    putExtra(ShareSheetActivity.EXTRA_CAMERA_NAME, cameraName)
                    putExtra(ShareSheetActivity.EXTRA_CAMERA_ID, cameraId)
                },
            )
        }
    }

    override fun onStop() {
        super.onStop()
        stopProgressTicker()
        lifecycleScope.launch {
            val sid = playbackSessionId
            playbackSessionId = null
            if (!sid.isNullOrBlank() && cameraId > 0L) {
                withContext(Dispatchers.IO) { repository.stopHistoryReplay(cameraId, sid) }
            }
        }
    }

    override fun onDestroy() {
        super.onDestroy()
        playerView.unbindPlayer()
        player?.stop()
        player?.release()
        player = null
    }

    private fun loadCameraInfo() {
        if (cameraId <= 0L) return
        lifecycleScope.launch {
            try {
                val info = withContext(Dispatchers.IO) { repository.getStreamInfo(cameraId) }
                applyCameraRuntimeSettings(info)
                val resolution = info.camera?.resolution
                    ?: if ((info.srs?.width ?: 0) > 0 && (info.srs?.height ?: 0) > 0) {
                        "${info.srs?.width}x${info.srs?.height}"
                    } else {
                        "Auto"
                    }
                val zh = isChineseLanguage(appConfig.getAppLanguage())
                findViewById<TextView>(R.id.event_detail_camera_meta).text =
                    "${info.camera?.name ?: if (zh) "摄像头" else "Camera"} · $resolution"
                val kbps = info.srs?.kbps?.recv_30s ?: 0
                findViewById<TextView>(R.id.event_detail_storage).text =
                    if (zh) "云端流 · ${kbps}kbps" else "Cloud stream · ${kbps}kbps"
            } catch (_: Exception) {
            }
        }
    }

    private fun applyCameraRuntimeSettings(info: com.reallive.android.network.StreamInfoDto) {
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
        val nightOn = cs.night_vision_enabled && cs.night_vision_mode.equals("auto", true)
        nightOverlay.visibility = if (nightOn) View.VISIBLE else View.GONE
        if (cs.watermark_enabled) {
            val cameraName = intent.getStringExtra(EXTRA_CAMERA_NAME)
                ?: if (isChineseLanguage(appConfig.getAppLanguage())) "摄像头" else "Camera"
            val labelTs = if (eventTs > 0L) eventTs else System.currentTimeMillis()
            val label = SimpleDateFormat("MM-dd HH:mm:ss", Locale.getDefault()).format(Date(labelTs))
            watermarkText.text = "$cameraName  $label"
            watermarkText.visibility = View.VISIBLE
        } else {
            watermarkText.visibility = View.GONE
        }
    }

    private fun loadPlayback(ts: Long) {
        if (cameraId <= 0L) return
        currentTsMs = ts
        lifecycleScope.launch {
            try {
                if (!playbackSessionId.isNullOrBlank()) {
                    val old = playbackSessionId
                    playbackSessionId = null
                    withContext(Dispatchers.IO) { repository.stopHistoryReplay(cameraId, old) }
                }
                val candidates = withContext(Dispatchers.IO) { buildPlaybackCandidates(ts) }
                var played = false
                candidates.forEach { candidateTs ->
                    if (played) return@forEach
                    val playback = withContext(Dispatchers.IO) {
                        repository.getHistoryPlayback(cameraId, candidateTs)
                    }
                    playbackSessionId = playback.sessionId
                    playbackUrl = playback.playbackUrl?.let { resolveMediaUrl(it) }
                    val mediaUrl = playbackUrl
                    if (!mediaUrl.isNullOrBlank() && playback.mode.lowercase(Locale.US) != "live") {
                        segmentStartMs = playback.segment?.startMs ?: candidateTs
                        val segDuration = playback.segment?.durationMs
                        val segEnd = playback.segment?.endMs ?: (segmentStartMs + (segDuration ?: 0L))
                        segmentDurationMs = when {
                            segDuration != null && segDuration > 0L -> segDuration
                            segEnd > segmentStartMs -> segEnd - segmentStartMs
                            else -> 60_000L
                        }
                        totalTimeText.text = formatDuration(segmentDurationMs)
                        findViewById<View>(R.id.event_detail_placeholder_icon).visibility = View.GONE
                        findViewById<View>(R.id.event_detail_play_overlay).visibility = View.GONE
                        val seekStartMs = (playback.offsetSec * 1000L).coerceAtLeast(0L)
                        pauseAtOffsetMs = seekStartMs
                        player?.playHistory(mediaUrl, seekStartMs)
                        isPaused = false
                        updatePlayPauseUi()
                        updateProgressUi((segmentStartMs + seekStartMs).coerceAtMost(segmentStartMs + segmentDurationMs))
                        startProgressTicker()
                        played = true
                    }
                }

                if (!played) {
                    player?.stop()
                    findViewById<View>(R.id.event_detail_placeholder_icon).visibility = View.VISIBLE
                    findViewById<View>(R.id.event_detail_play_overlay).visibility = View.VISIBLE
                    stopProgressTicker()
                    Toast.makeText(
                        this@EventDetailActivity,
                        if (isChineseLanguage(appConfig.getAppLanguage())) "该事件暂无历史录像" else "No recording available for this event.",
                        Toast.LENGTH_SHORT,
                    ).show()
                }
            } catch (_: Exception) {
            }
        }
    }

    private fun resolveMediaUrl(raw: String): String {
        if (raw.startsWith("http://") || raw.startsWith("https://")) return raw
        val base = appConfig.getBaseUrl().removeSuffix("/")
        val path = if (raw.startsWith("/")) raw else "/$raw"
        return base + path
    }

    private suspend fun buildPlaybackCandidates(ts: Long): List<Long> {
        val start = startOfDay(ts)
        val end = start + 24 * 60 * 60 * 1000L - 1L
        val timeline = repository.getTimeline(cameraId, start, end)
        val containing = timeline.segments.firstOrNull { ts in it.startMs..it.endMs }?.startMs?.plus(500L)
        val nearest = timeline.segments.minByOrNull {
            kotlin.math.abs(((it.startMs + it.endMs) / 2L) - ts)
        }?.startMs?.plus(500L)
        return listOf(ts, containing, nearest).filterNotNull().distinct()
    }

    private fun startOfDay(tsMs: Long): Long {
        val cal = java.util.Calendar.getInstance().apply {
            timeInMillis = tsMs
            set(java.util.Calendar.HOUR_OF_DAY, 0)
            set(java.util.Calendar.MINUTE, 0)
            set(java.util.Calendar.SECOND, 0)
            set(java.util.Calendar.MILLISECOND, 0)
        }
        return cal.timeInMillis
    }

    private fun applyLocalizedTexts(zh: Boolean, eventType: String) {
        pageTitleText.text = if (zh) "事件详情" else "Event Detail"
        badgeTypeText.text = when {
            eventType.lowercase(Locale.US).contains("person") -> if (zh) "人物检测" else "Person Detected"
            eventType.lowercase(Locale.US).contains("motion") -> if (zh) "移动检测" else "Motion Detected"
            else -> if (zh) "事件" else "Event"
        }
        badgePriorityText.text = if (zh) "高优先级" else "High Priority"
        zoneTitleText.text = if (zh) "检测区域" else "Detection Zone"
        confidenceTitleText.text = if (zh) "置信度" else "Confidence"
        storageTitleText.text = if (zh) "存储" else "Storage"
        downloadText.text = if (zh) "下载片段" else "Download Clip"
        shareText.text = if (zh) "分享事件" else "Share Event"
    }

    private fun bindPlaybackControls() {
        seekBar.max = 1000
        totalTimeText.text = "00:00"
        currentTimeText.text = "00:00"
        updatePlayPauseUi()
        seekBar.setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
            override fun onProgressChanged(seekBar: SeekBar?, progress: Int, fromUser: Boolean) {
                if (!fromUser || segmentDurationMs <= 0L) return
                val offsetMs = (segmentDurationMs * progress / 1000.0).toLong()
                currentTimeText.text = formatDuration(offsetMs)
            }

            override fun onStartTrackingTouch(seekBar: SeekBar?) {
                isUserSeeking = true
            }

            override fun onStopTrackingTouch(seekBar: SeekBar?) {
                if (segmentStartMs <= 0L || segmentDurationMs <= 0L) {
                    isUserSeeking = false
                    return
                }
                val p = (seekBar?.progress ?: 0).coerceIn(0, 1000)
                val targetTs = segmentStartMs + (segmentDurationMs * p / 1000.0).toLong()
                isUserSeeking = false
                isPaused = false
                updatePlayPauseUi()
                loadPlayback(targetTs)
            }
        })
        findViewById<View>(R.id.event_detail_rewind_10s).setOnClickListener {
            if (segmentStartMs <= 0L) return@setOnClickListener
            val base = if (currentTsMs > 0L) currentTsMs else eventTs
            val target = (base - 10_000L).coerceAtLeast(segmentStartMs)
            isPaused = false
            updatePlayPauseUi()
            loadPlayback(target)
        }
        findViewById<View>(R.id.event_detail_forward_10s).setOnClickListener {
            if (segmentStartMs <= 0L || segmentDurationMs <= 0L) return@setOnClickListener
            val segEnd = segmentStartMs + segmentDurationMs
            val base = if (currentTsMs > 0L) currentTsMs else eventTs
            val target = (base + 10_000L).coerceAtMost(segEnd)
            isPaused = false
            updatePlayPauseUi()
            loadPlayback(target)
        }
        playPauseButton.setOnClickListener {
            if (segmentStartMs <= 0L) return@setOnClickListener
            if (isPaused) {
                isPaused = false
                updatePlayPauseUi()
                player?.resume()
            } else {
                val stats = player?.getStats()
                pauseAtOffsetMs = (stats?.currentPositionMs ?: pauseAtOffsetMs).coerceAtLeast(0L)
                isPaused = true
                updatePlayPauseUi()
                player?.pause()
            }
        }
    }

    private fun updatePlayPauseUi() {
        playPauseButton.setImageResource(if (isPaused) R.drawable.ic_rl_play_24 else R.drawable.ic_rl_pause_24)
    }

    private fun updateProgressUi(tsMs: Long) {
        if (segmentStartMs <= 0L || segmentDurationMs <= 0L) return
        currentTsMs = tsMs
        val offset = (tsMs - segmentStartMs).coerceIn(0L, segmentDurationMs)
        pauseAtOffsetMs = offset
        if (!isUserSeeking) {
            val progress = ((offset.toDouble() / segmentDurationMs.toDouble()) * 1000.0).toInt().coerceIn(0, 1000)
            seekBar.progress = progress
        }
        currentTimeText.text = formatDuration(offset)
    }

    private fun startProgressTicker() {
        if (isProgressTickerActive) return
        isProgressTickerActive = true
        lastTickAtUptimeMs = 0L
        seekBar.post(progressTicker)
    }

    private fun stopProgressTicker() {
        if (!isProgressTickerActive) return
        isProgressTickerActive = false
        seekBar.removeCallbacks(progressTicker)
    }

    private fun formatDuration(ms: Long): String {
        val totalSec = (ms / 1000L).coerceAtLeast(0L)
        val mm = totalSec / 60L
        val ss = totalSec % 60L
        return String.format(Locale.US, "%02d:%02d", mm, ss)
    }

    private fun localeForLanguage(languageCode: String?): Locale {
        if (languageCode.isNullOrBlank()) return Locale.getDefault()
        return Locale.forLanguageTag(languageCode.replace('_', '-'))
    }

    private fun isChineseLanguage(languageCode: String?): Boolean {
        if (languageCode.isNullOrBlank()) return false
        return languageCode.lowercase(Locale.US).startsWith("zh")
    }

    companion object {
        const val EXTRA_EVENT_TYPE = "extra_event_type"
        const val EXTRA_EVENT_TS = "extra_event_ts"
        const val EXTRA_EVENT_SCORE = "extra_event_score"
        const val EXTRA_CAMERA_NAME = "extra_camera_name"
        const val EXTRA_CAMERA_ID = "extra_camera_id"
    }
}
