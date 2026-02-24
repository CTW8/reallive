package com.reallive.android.ui.history

import android.content.Intent
import android.os.Bundle
import androidx.appcompat.app.AppCompatActivity
import androidx.lifecycle.lifecycleScope
import android.view.View
import android.view.ViewGroup
import android.view.MotionEvent
import android.view.ScaleGestureDetector
import android.widget.FrameLayout
import android.widget.ImageButton
import android.widget.ImageView
import android.widget.LinearLayout
import android.widget.SeekBar
import android.widget.TextView
import android.widget.Toast
import androidx.activity.result.contract.ActivityResultContracts
import androidx.recyclerview.widget.LinearLayoutManager
import androidx.recyclerview.widget.RecyclerView
import com.reallive.android.R
import com.reallive.android.config.AppConfig
import com.reallive.android.data.CameraRepository
import com.reallive.android.network.ApiFactory
import com.reallive.android.network.HistorySegmentDto
import com.reallive.android.network.HistoryThumbnailDto
import com.reallive.android.network.HistoryTimelineEventDto
import com.reallive.android.network.HistoryTimelineDto
import com.reallive.android.network.StreamInfoDto
import com.reallive.player.Player
import com.reallive.player.PlayerFactory
import com.reallive.player.PlayerSurfaceView
import com.reallive.android.ui.auth.LoginActivity
import com.reallive.android.ui.auth.AuthGuard
import com.reallive.android.ui.watch.CalendarPickerActivity
import com.reallive.android.ui.watch.EventDetailActivity
import com.reallive.android.ui.watch.TimelineEventAdapter
import com.reallive.android.ui.watch.TimelineEventItem
import com.reallive.android.ui.watch.WatchActivity
import android.os.SystemClock
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import coil.load
import retrofit2.HttpException
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

class HistoryActivity : AppCompatActivity() {
    private lateinit var adapter: TimelineEventAdapter
    private lateinit var appConfig: AppConfig
    private lateinit var repository: CameraRepository
    private var cameraId: Long = -1L
    private var cameraName: String = "Camera"
    private lateinit var titleText: TextView
    private lateinit var dateText: TextView
    private lateinit var speedText: TextView
    private lateinit var playPauseButton: ImageButton
    private lateinit var playerTimeRow: View
    private lateinit var playerProgressArea: View
    private lateinit var seekBar: SeekBar
    private lateinit var timelineStatic: View
    private lateinit var timelineDynamic: View
    private lateinit var timelineScroll: android.widget.HorizontalScrollView
    private lateinit var timelineContent: LinearLayout
    private lateinit var timelineLeftSpacer: View
    private lateinit var timelineRightSpacer: View
    private lateinit var timelineTrackContainer: FrameLayout
    private lateinit var timelineRulerLayer: FrameLayout
    private lateinit var timelineClipLayer: FrameLayout
    private lateinit var timelineMarkerLayer: FrameLayout
    private lateinit var timelineTick0: TextView
    private lateinit var timelineTick1: TextView
    private lateinit var timelineTick2: TextView
    private lateinit var timelineTick3: TextView
    private lateinit var timelineTick4: TextView
    private lateinit var playheadTimeText: TextView
    private lateinit var playerView: PlayerSurfaceView
    private lateinit var placeholderIcon: View
    private lateinit var nightOverlay: View
    private lateinit var watermarkText: TextView
    private var player: Player? = null
    private var playbackSessionId: String? = null
    private var currentPlaybackTsMs: Long = -1L
    private var currentSpeedIndex: Int = 1
    private var isPlaybackRunning: Boolean = false
    private var selectedDayStartMs: Long = startOfDay(System.currentTimeMillis())
    private var allEvents: List<TimelineEventItem> = emptyList()
    private var filteredEvents: List<TimelineEventItem> = emptyList()
    private var activeFilter: String = "all"
    private var timelineStartMs: Long = 0L
    private var timelineEndMs: Long = 0L
    private var timelineZoom: Float = 1.0f
    private var baseTimelineWidthPx: Int = 0
    private var timelineSidePadPx: Int = 0
    private var activeTimeline: HistoryTimelineDto? = null
    private lateinit var scaleDetector: ScaleGestureDetector
    private var isTimelineUserInteracting: Boolean = false
    private var lastTimelineTouchUptimeMs: Long = 0L
    private var suppressSeekOnNextUp: Boolean = false
    private var pendingZoomRender: Boolean = false
    private var pendingZoomFocusTs: Long = 0L
    private var pendingZoomFocusX: Float = 0f
    private var lastZoomRenderAtMs: Long = 0L
    private var lastRenderedZoom: Float = 1.0f
    private var playbackTickerActive: Boolean = false
    private var lastPlaybackTickUptimeMs: Long = 0L
    private var currentPlaybackSegmentStartMs: Long = -1L
    private var playbackNativeBasePosMs: Long = -1L
    private var playbackNativeBaseTsMs: Long = -1L
    private var playbackAnchorTsMs: Long = -1L
    private var playbackAnchorUptimeMs: Long = 0L
    private val zoomRenderRunnable = Runnable {
        pendingZoomRender = false
        val timeline = activeTimeline ?: return@Runnable
        renderTimeline(timeline)
        val targetX = tsToX(pendingZoomFocusTs) - pendingZoomFocusX.toInt()
        timelineScroll.scrollTo(targetX.coerceAtLeast(0), 0)
        updateTimelineTickLabels()
        lastZoomRenderAtMs = SystemClock.uptimeMillis()
        lastRenderedZoom = timelineZoom
    }
    private val playbackTickerRunnable = object : Runnable {
        override fun run() {
            if (!playbackTickerActive) return
            val now = SystemClock.uptimeMillis()
            lastPlaybackTickUptimeMs = now
            updateRuntimeWatermark()
            if (isTimelineUserInteracting && now - lastTimelineTouchUptimeMs > 700L) {
                isTimelineUserInteracting = false
            }
            val stats = player?.getStats()
            if ((isPlaybackRunning || !playbackSessionId.isNullOrBlank()) &&
                !isTimelineUserInteracting
            ) {
                val dayEnd = selectedDayStartMs + 24 * 60 * 60 * 1000L
                val nativePosMs = stats?.currentPositionMs ?: -1L
                if (nativePosMs >= 0L) {
                    var synced = false
                    if (playbackNativeBasePosMs >= 0L && playbackNativeBaseTsMs > 0L) {
                        val delta = nativePosMs - playbackNativeBasePosMs
                        currentPlaybackTsMs = (playbackNativeBaseTsMs + delta)
                            .coerceIn(selectedDayStartMs, dayEnd)
                        syncTimelineToTs(currentPlaybackTsMs)
                        synced = true
                    } else if (currentPlaybackSegmentStartMs > 0L) {
                        currentPlaybackTsMs = (currentPlaybackSegmentStartMs + nativePosMs)
                            .coerceIn(selectedDayStartMs, dayEnd)
                        syncTimelineToTs(currentPlaybackTsMs)
                        synced = true
                    }
                    if (!synced && playbackAnchorTsMs > 0L) {
                        val speed = when (SPEED_LABELS[currentSpeedIndex]) {
                            "0.5x" -> 0.5
                            "2x" -> 2.0
                            else -> 1.0
                        }
                        val elapsedMs = (now - playbackAnchorUptimeMs).coerceAtLeast(0L)
                        val advanceMs = (elapsedMs * speed).toLong().coerceAtLeast(0L)
                        currentPlaybackTsMs = (playbackAnchorTsMs + advanceMs).coerceIn(selectedDayStartMs, dayEnd)
                        syncTimelineToTs(currentPlaybackTsMs)
                    }
                } else if (playbackAnchorTsMs > 0L) {
                    val speed = when (SPEED_LABELS[currentSpeedIndex]) {
                        "0.5x" -> 0.5
                        "2x" -> 2.0
                        else -> 1.0
                    }
                    val elapsedMs = (now - playbackAnchorUptimeMs).coerceAtLeast(0L)
                    val advanceMs = (elapsedMs * speed).toLong().coerceAtLeast(0L)
                    currentPlaybackTsMs = (playbackAnchorTsMs + advanceMs).coerceIn(selectedDayStartMs, dayEnd)
                    syncTimelineToTs(currentPlaybackTsMs)
                }
            }

            val state = stats?.state
            if (state == com.reallive.player.PlayerState.ENDED ||
                state == com.reallive.player.PlayerState.ERROR
            ) {
                isPlaybackRunning = false
                updatePlayPauseUi()
            }

            timelineDynamic.postDelayed(this, 120L)
        }
    }

    private val calendarLauncher =
        registerForActivityResult(ActivityResultContracts.StartActivityForResult()) { result ->
            if (result.resultCode != RESULT_OK) return@registerForActivityResult
            val day = result.data?.getLongExtra(CalendarPickerActivity.RESULT_SELECTED_DAY_START_MS, -1L) ?: -1L
            if (day > 0L) {
                selectedDayStartMs = startOfDay(day)
                updateDateHeader()
                currentPlaybackTsMs = -1L
                loadTimeline()
            }
        }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        appConfig = AppConfig(this)
        if (appConfig.getToken().isNullOrBlank()) {
            redirectToLogin()
            return
        }
        repository = CameraRepository(ApiFactory.createAuthorized(appConfig))
        setContentView(R.layout.activity_history)

        findViewById<android.view.View>(R.id.history_back).setOnClickListener {
            navigateBackByDesign()
        }
        findViewById<android.view.View>(R.id.history_calendar).setOnClickListener {
            calendarLauncher.launch(
                Intent(this, CalendarPickerActivity::class.java).apply {
                    putExtra(CalendarPickerActivity.EXTRA_SELECTED_DAY_START_MS, selectedDayStartMs)
                },
            )
        }
        findViewById<View>(R.id.history_calendar_prev).setOnClickListener {
            selectedDayStartMs -= 24 * 60 * 60 * 1000L
            updateDateHeader()
            currentPlaybackTsMs = -1L
            loadTimeline()
        }
        findViewById<View>(R.id.history_calendar_next).setOnClickListener {
            selectedDayStartMs += 24 * 60 * 60 * 1000L
            updateDateHeader()
            currentPlaybackTsMs = -1L
            loadTimeline()
        }

        cameraId = intent.getLongExtra(WatchActivity.EXTRA_CAMERA_ID, -1L)
        cameraName = intent.getStringExtra(WatchActivity.EXTRA_CAMERA_NAME)
            ?: if (isChineseLanguage(appConfig.getAppLanguage())) "摄像头" else "Camera"
        val intentSelectedDay = intent.getLongExtra(EXTRA_SELECTED_DAY_START_MS, -1L)
        if (intentSelectedDay > 0L) {
            selectedDayStartMs = startOfDay(intentSelectedDay)
        }
        val startTs = intent.getLongExtra(
            com.reallive.android.ui.watch.snapshot.SnapshotGalleryActivity.EXTRA_START_TS,
            -1L,
        )
        if (startTs > 0L) {
            currentPlaybackTsMs = startTs
            lifecycleScope.launch {
                loadPlayback(startTs)
            }
        }
        titleText = findViewById(R.id.history_title)
        dateText = findViewById(R.id.history_date_text)
        speedText = findViewById(R.id.history_speed)
        playPauseButton = findViewById(R.id.history_btn_play_pause)
        playerTimeRow = findViewById(R.id.history_player_time_row)
        playerProgressArea = findViewById(R.id.history_player_progress_area)
        seekBar = findViewById(R.id.history_player_seek)
        timelineStatic = findViewById(R.id.history_timeline_static)
        timelineDynamic = findViewById(R.id.history_timeline_dynamic)
        timelineScroll = findViewById(R.id.history_timeline_thumb_scroll)
        timelineContent = findViewById(R.id.history_timeline_content)
        timelineLeftSpacer = findViewById(R.id.history_timeline_left_spacer)
        timelineRightSpacer = findViewById(R.id.history_timeline_right_spacer)
        timelineTrackContainer = findViewById(R.id.history_timeline_track_container)
        timelineRulerLayer = findViewById(R.id.history_timeline_ruler_layer)
        timelineClipLayer = findViewById(R.id.history_timeline_clip_layer)
        timelineMarkerLayer = findViewById(R.id.history_timeline_event_marker_layer)
        timelineTick0 = findViewById(R.id.history_timeline_tick_0)
        timelineTick1 = findViewById(R.id.history_timeline_tick_1)
        timelineTick2 = findViewById(R.id.history_timeline_tick_2)
        timelineTick3 = findViewById(R.id.history_timeline_tick_3)
        timelineTick4 = findViewById(R.id.history_timeline_tick_4)
        playheadTimeText = findViewById(R.id.history_playhead_time)
        titleText.text = cameraName
        applyLocalizedTexts(appConfig.getAppLanguage())
        updateDateHeader()
        playerView = findViewById(R.id.history_player_view)
        placeholderIcon = findViewById(R.id.history_placeholder_icon)
        nightOverlay = findViewById(R.id.history_night_overlay)
        watermarkText = findViewById(R.id.history_watermark)
        player = PlayerFactory.create()
        player?.let { playerView.bindPlayer(it) }

        adapter = TimelineEventAdapter { evt ->
            currentPlaybackTsMs = evt.tsMs
            lifecycleScope.launch { loadPlayback(evt.tsMs) }
            openEventDetail(evt)
        }
        findViewById<RecyclerView>(R.id.history_recycler).apply {
            layoutManager = LinearLayoutManager(this@HistoryActivity)
            adapter = this@HistoryActivity.adapter
        }

        bindPlaybackActions()
        bindFilterActions()
        bindTimelineInteractions()
        updateSpeedUi()
        updatePlayPauseUi()
        hideVideoTimelineControls()
    }

    override fun onStart() {
        super.onStart()
        loadCameraRuntimeSettings()
        loadTimeline()
    }

    override fun onStop() {
        super.onStop()
        stopPlaybackTicker()
        lifecycleScope.launch {
            val sessionId = playbackSessionId
            playbackSessionId = null
            isPlaybackRunning = false
            updatePlayPauseUi()
            if (!sessionId.isNullOrBlank() && cameraId > 0L) {
                withContext(Dispatchers.IO) { repository.stopHistoryReplay(cameraId, sessionId) }
            }
        }
    }

    override fun onDestroy() {
        super.onDestroy()
        stopPlaybackTicker()
        timelineDynamic.removeCallbacks(zoomRenderRunnable)
        playerView.unbindPlayer()
        player?.stop()
        player?.release()
        player = null
    }

    private fun loadTimeline() {
        if (cameraId <= 0L) return
        lifecycleScope.launch {
            try {
                val start = selectedDayStartMs
                val end = start + 24 * 60 * 60 * 1000L - 1L
                val timeline = withContext(Dispatchers.IO) {
                    repository.getTimeline(cameraId, start, end)
                }
                activeTimeline = timeline
                timelineStartMs = start
                timelineEndMs = start + 24 * 60 * 60 * 1000L
                renderTimeline(timeline)
                allEvents = timeline.events.map { mapEvent(it, timeline) }.sortedBy { it.tsMs }
                applyFilterAndRenderEvents()
                if (currentPlaybackTsMs <= 0L) {
                    val ts = filteredEvents.firstOrNull()?.tsMs ?: start
                    currentPlaybackTsMs = ts
                    loadPlayback(ts)
                }
            } catch (ex: Exception) {
                if (ex is HttpException && ex.code() == 401) {
                    val valid = withContext(Dispatchers.IO) { AuthGuard.isSessionValid(appConfig) }
                    if (!valid) {
                        appConfig.clearAuth()
                        redirectToLogin()
                    }
                }
            }
        }
    }

    private fun loadPlayback(tsMs: Long) {
        if (cameraId <= 0L) return
        lifecycleScope.launch {
            try {
                if (!playbackSessionId.isNullOrBlank()) {
                    val old = playbackSessionId
                    playbackSessionId = null
                    withContext(Dispatchers.IO) {
                        repository.stopHistoryReplay(cameraId, old)
                    }
                }
                val playback = withContext(Dispatchers.IO) {
                    repository.getHistoryPlayback(cameraId, tsMs)
                }
                playbackSessionId = playback.sessionId
                val url = playback.playbackUrl?.let { resolveMediaUrl(it) }
                if (!url.isNullOrBlank()) {
                    placeholderIcon.visibility = View.GONE
                    currentPlaybackTsMs = tsMs
                    syncTimelineToTs(tsMs)
                    val mode = playback.mode.lowercase(Locale.US)
                    if (mode == "live") {
                        stopPlaybackTicker()
                        player?.stop()
                        currentPlaybackSegmentStartMs = -1L
                        playbackNativeBasePosMs = -1L
                        playbackNativeBaseTsMs = -1L
                        placeholderIcon.visibility = View.VISIBLE
                        isPlaybackRunning = false
                        updatePlayPauseUi()
                        Toast.makeText(
                            this@HistoryActivity,
                            if (isChineseLanguage(appConfig.getAppLanguage())) "当前时间点没有历史录像" else "No recording at this time.",
                            Toast.LENGTH_SHORT,
                        ).show()
                    } else {
                        val seekStartMs = (playback.offsetSec * 1000L).coerceAtLeast(0L)
                        currentPlaybackSegmentStartMs = playback.segment?.startMs
                            ?: (tsMs - seekStartMs).coerceAtLeast(0L)
                        playbackNativeBasePosMs = seekStartMs
                        playbackNativeBaseTsMs = tsMs
                        player?.playHistory(url, seekStartMs)
                        isPlaybackRunning = true
                        updatePlayPauseUi()
                        playbackAnchorTsMs = tsMs
                        playbackAnchorUptimeMs = SystemClock.uptimeMillis()
                        startPlaybackTicker()
                    }
                } else {
                    stopPlaybackTicker()
                    currentPlaybackSegmentStartMs = -1L
                    playbackNativeBasePosMs = -1L
                    playbackNativeBaseTsMs = -1L
                    placeholderIcon.visibility = View.VISIBLE
                    isPlaybackRunning = false
                    updatePlayPauseUi()
                    Toast.makeText(
                        this@HistoryActivity,
                        if (isChineseLanguage(appConfig.getAppLanguage())) "未找到历史录像片段" else "No history segment found.",
                        Toast.LENGTH_SHORT,
                    ).show()
                }
            } catch (ex: Exception) {
                stopPlaybackTicker()
                currentPlaybackSegmentStartMs = -1L
                playbackNativeBasePosMs = -1L
                playbackNativeBaseTsMs = -1L
                isPlaybackRunning = false
                updatePlayPauseUi()
                if (ex is HttpException && ex.code() == 401) {
                    val valid = withContext(Dispatchers.IO) { AuthGuard.isSessionValid(appConfig) }
                    if (!valid) {
                        appConfig.clearAuth()
                        redirectToLogin()
                    }
                }
            }
        }
    }

    private fun mapEvent(event: HistoryTimelineEventDto, timeline: HistoryTimelineDto): TimelineEventItem {
        val thumbUrl = resolveEventThumbnailUrl(event.ts, timeline.thumbnails, timeline.segments)
        return TimelineEventItem(
            tsMs = event.ts,
            type = event.type,
            score = event.score ?: 0.0,
            thumbnailUrl = thumbUrl,
        )
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

    private fun redirectToLogin() {
        startActivity(Intent(this, LoginActivity::class.java))
        finish()
    }

    private fun updateDateHeader() {
        val text = SimpleDateFormat("MMM dd, yyyy", localeForLanguage(appConfig.getAppLanguage()))
            .format(Date(selectedDayStartMs))
        dateText.text = text
    }

    private fun navigateBackByDesign() {
        if (isTaskRoot) {
            startActivity(
                Intent(this, WatchActivity::class.java).apply {
                    putExtra(WatchActivity.EXTRA_CAMERA_ID, cameraId)
                    putExtra(WatchActivity.EXTRA_CAMERA_NAME, cameraName)
                },
            )
        }
        finish()
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

    private fun bindPlaybackActions() {
        findViewById<View>(R.id.history_speed).setOnClickListener {
            currentSpeedIndex = (currentSpeedIndex + 1) % SPEED_LABELS.size
            updateSpeedUi()
        }
        playPauseButton.setOnClickListener {
            if (isPlaybackRunning) {
                player?.stop()
                stopPlaybackTicker()
                isPlaybackRunning = false
                updatePlayPauseUi()
            } else if (currentPlaybackTsMs > 0L) {
                lifecycleScope.launch { loadPlayback(currentPlaybackTsMs) }
            }
        }
        findViewById<View>(R.id.history_btn_rewind).setOnClickListener {
            if (currentPlaybackTsMs > 0L) {
                lifecycleScope.launch { loadPlayback(currentPlaybackTsMs - jumpStepMs()) }
            }
        }
        findViewById<View>(R.id.history_btn_fast_forward).setOnClickListener {
            if (currentPlaybackTsMs > 0L) {
                lifecycleScope.launch { loadPlayback(currentPlaybackTsMs + jumpStepMs()) }
            }
        }
        findViewById<View>(R.id.history_btn_skip_prev).setOnClickListener {
            val prev = filteredEvents.lastOrNull { it.tsMs < currentPlaybackTsMs }
            if (prev != null) lifecycleScope.launch { loadPlayback(prev.tsMs) }
        }
        findViewById<View>(R.id.history_btn_skip_next).setOnClickListener {
            val next = filteredEvents.firstOrNull { it.tsMs > currentPlaybackTsMs }
            if (next != null) lifecycleScope.launch { loadPlayback(next.tsMs) }
        }
        seekBar.setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
            override fun onProgressChanged(seekBar: SeekBar?, progress: Int, fromUser: Boolean) {
                // top seekbar disabled by design
            }

            override fun onStartTrackingTouch(seekBar: SeekBar?) {
            }

            override fun onStopTrackingTouch(seekBar: SeekBar?) {
            }
        })
        seekBar.isEnabled = false
    }

    private fun bindFilterActions() {
        findViewById<TextView>(R.id.history_filter_all).setOnClickListener { setFilter("all") }
        findViewById<TextView>(R.id.history_filter_motion).setOnClickListener { setFilter("motion") }
        findViewById<TextView>(R.id.history_filter_person).setOnClickListener { setFilter("person") }
        findViewById<TextView>(R.id.history_filter_alert).setOnClickListener { setFilter("alert") }
        setFilter(activeFilter)
    }

    private fun setFilter(filter: String) {
        activeFilter = filter
        updateFilterChip(findViewById(R.id.history_filter_all), activeFilter == "all")
        updateFilterChip(findViewById(R.id.history_filter_motion), activeFilter == "motion")
        updateFilterChip(findViewById(R.id.history_filter_person), activeFilter == "person")
        updateFilterChip(findViewById(R.id.history_filter_alert), activeFilter == "alert")
        applyFilterAndRenderEvents()
    }

    private fun applyFilterAndRenderEvents() {
        if (!::adapter.isInitialized) return
        filteredEvents = allEvents.filter { evt ->
            when (activeFilter) {
                "motion" -> evt.type.lowercase(Locale.US).contains("motion")
                "person" -> evt.type.lowercase(Locale.US).contains("person")
                "alert" -> evt.score >= 0.7
                else -> true
            }
        }
        adapter.submitList(filteredEvents)
    }

    private fun updateFilterChip(chip: TextView, selected: Boolean) {
        if (selected) {
            chip.setBackgroundResource(R.drawable.bg_btn_tonal)
            chip.setTextColor(getColor(R.color.auth_primary))
        } else {
            chip.setBackgroundResource(R.drawable.bg_btn_outline)
            chip.setTextColor(getColor(R.color.auth_on_surface_variant))
        }
    }

    private fun updateSpeedUi() {
        speedText.text = SPEED_LABELS[currentSpeedIndex]
    }

    private fun jumpStepMs(): Long {
        return when (SPEED_LABELS[currentSpeedIndex]) {
            "0.5x" -> 5_000L
            "2x" -> 20_000L
            else -> 10_000L
        }
    }

    private fun updatePlayPauseUi() {
        val icon = if (isPlaybackRunning) R.drawable.ic_rl_pause_24 else R.drawable.ic_rl_play_24
        playPauseButton.setImageResource(icon)
    }

    private fun loadCameraRuntimeSettings() {
        if (cameraId <= 0L) return
        lifecycleScope.launch {
            try {
                val info = withContext(Dispatchers.IO) { repository.getStreamInfo(cameraId) }
                applyCameraRuntimeSettings(info)
            } catch (_: Exception) {
            }
        }
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
        val nightOn = cs.night_vision_enabled && cs.night_vision_mode.equals("auto", true)
        nightOverlay.visibility = if (nightOn) View.VISIBLE else View.GONE
        watermarkText.visibility = if (cs.watermark_enabled) View.VISIBLE else View.GONE
        updateRuntimeWatermark()
    }

    private fun updateRuntimeWatermark() {
        if (!::watermarkText.isInitialized || watermarkText.visibility != View.VISIBLE) return
        val ts = if (currentPlaybackTsMs > 0L) currentPlaybackTsMs else System.currentTimeMillis()
        val label = SimpleDateFormat("MM-dd HH:mm:ss", Locale.getDefault()).format(Date(ts))
        watermarkText.text = "$cameraName  $label"
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

    private fun formatClock(ts: Long): String {
        return SimpleDateFormat("HH:mm:ss", Locale.getDefault()).format(Date(ts))
    }

    private fun resolveMediaUrl(raw: String): String {
        if (raw.startsWith("http://") || raw.startsWith("https://")) return raw
        val base = appConfig.getBaseUrl().removeSuffix("/")
        val path = if (raw.startsWith("/")) raw else "/$raw"
        return base + path
    }

    private fun applyLocalizedTexts(languageCode: String) {
        val zh = isChineseLanguage(languageCode)
        findViewById<TextView>(R.id.history_filter_all).text = if (zh) "全部" else "All"
        findViewById<TextView>(R.id.history_filter_motion).text = if (zh) "移动" else "Motion"
        findViewById<TextView>(R.id.history_filter_person).text = if (zh) "人物" else "Person"
        findViewById<TextView>(R.id.history_filter_alert).text = if (zh) "告警" else "Alert"
    }

    private fun localeForLanguage(languageCode: String?): Locale {
        if (languageCode.isNullOrBlank()) return Locale.getDefault()
        val normalized = languageCode.replace('_', '-')
        return Locale.forLanguageTag(normalized)
    }

    private fun isChineseLanguage(languageCode: String?): Boolean {
        if (languageCode.isNullOrBlank()) return false
        return languageCode.lowercase(Locale.US).startsWith("zh")
    }

    private fun renderTimeline(timeline: HistoryTimelineDto) {
        timelineStatic.visibility = View.GONE
        timelineDynamic.visibility = View.VISIBLE
        if (baseTimelineWidthPx <= 0) {
            baseTimelineWidthPx = resources.displayMetrics.widthPixels - 40.dp()
        }
        renderNleTrack(timeline)
        renderEventMarkers(timeline.events)
        updateTimelineTickLabels()
        timelineScroll.post {
            val desiredPad = timelineRequiredSidePadPx()
            if (desiredPad != timelineSidePadPx) {
                renderNleTrack(timeline)
                renderEventMarkers(timeline.events)
                updateTimelineTickLabels()
                if (currentPlaybackTsMs > 0L) {
                    syncTimelineToTs(currentPlaybackTsMs)
                }
            }
        }
    }

    private fun renderNleTrack(timeline: HistoryTimelineDto) {
        timelineRulerLayer.removeAllViews()
        timelineClipLayer.removeAllViews()
        val segments = timeline.segments
        val contentWidth = timelineWidthPx()
        timelineSidePadPx = timelineRequiredSidePadPx()
        timelineContent.layoutParams = timelineContent.layoutParams.apply {
            width = contentWidth + timelineSidePadPx * 2
        }
        timelineLeftSpacer.layoutParams = (timelineLeftSpacer.layoutParams as LinearLayout.LayoutParams).apply {
            width = timelineSidePadPx
        }
        timelineRightSpacer.layoutParams = (timelineRightSpacer.layoutParams as LinearLayout.LayoutParams).apply {
            width = timelineSidePadPx
        }
        timelineTrackContainer.layoutParams = (timelineTrackContainer.layoutParams as LinearLayout.LayoutParams).apply {
            width = contentWidth
        }
        timelineRulerLayer.layoutParams = (timelineRulerLayer.layoutParams as FrameLayout.LayoutParams).apply {
            width = contentWidth
        }
        timelineClipLayer.layoutParams = (timelineClipLayer.layoutParams as FrameLayout.LayoutParams).apply {
            width = contentWidth
        }
        timelineMarkerLayer.layoutParams = (timelineMarkerLayer.layoutParams as FrameLayout.LayoutParams).apply {
            width = contentWidth
        }
        renderNleRuler(contentWidth)
        renderNleSegments(segments, timeline.thumbnails.sortedBy { it.ts })
    }

    private fun renderNleRuler(contentWidth: Int) {
        val totalSeconds = 24 * 60 * 60
        val minorStepSeconds = when {
            timelineZoom >= 29.5f -> 2 * 60
            timelineZoom >= 28.0f -> 3 * 60
            timelineZoom >= 24.0f -> 5 * 60
            timelineZoom >= 20.0f -> 10 * 60
            timelineZoom >= 14.0f -> 15 * 60
            timelineZoom >= 9.0f -> 5 * 60
            timelineZoom >= 6.0f -> 10 * 60
            timelineZoom >= 3.5f -> 15 * 60
            timelineZoom >= 2.0f -> 30 * 60
            else -> 60 * 60
        }
        val labelStepSeconds = when {
            timelineZoom >= 24.0f -> 30 * 60
            timelineZoom >= 14.0f -> 60 * 60
            timelineZoom >= 8.0f -> 2 * 60 * 60
            timelineZoom >= 4.0f -> 4 * 60 * 60
            else -> 6 * 60 * 60
        }
        val steps = totalSeconds / minorStepSeconds
        for (i in 0..steps) {
            val seconds = (i * minorStepSeconds).coerceAtMost(totalSeconds)
            val ratio = seconds.toFloat() / totalSeconds.toFloat()
            val x = (ratio * contentWidth).toInt()
            val isHour = seconds % 3600 == 0
            val isLabelTick = seconds % labelStepSeconds == 0
            val hour = seconds / 3600
            val tick = View(this).apply {
                layoutParams = FrameLayout.LayoutParams(
                    1.dp(),
                    when {
                        isHour && hour % 6 == 0 -> 16.dp()
                        isHour -> 13.dp()
                        else -> 8.dp()
                    },
                ).apply {
                    leftMargin = x.coerceAtLeast(0)
                    topMargin = 8.dp()
                }
                setBackgroundColor(0x66FFFFFF.toInt())
            }
            timelineRulerLayer.addView(tick)
            val shouldLabel = seconds == 0 || seconds == totalSeconds || isLabelTick
            if (shouldLabel) {
                val label = TextView(this).apply {
                    text = when {
                        seconds == totalSeconds -> "24:00"
                        else -> {
                            val hh = seconds / 3600
                            val mm = (seconds % 3600) / 60
                            String.format(Locale.US, "%02d:%02d", hh, mm)
                        }
                    }
                    textSize = 10f
                    setTextColor(0xB3FFFFFF.toInt())
                    val labelWidth = 56.dp()
                    gravity = when (hour) {
                        0 -> android.view.Gravity.START
                        24 -> android.view.Gravity.END
                        else -> android.view.Gravity.CENTER
                    }
                    layoutParams = FrameLayout.LayoutParams(
                        labelWidth,
                        ViewGroup.LayoutParams.WRAP_CONTENT,
                    ).apply {
                        leftMargin = when (hour) {
                            0 -> 0
                            24 -> (contentWidth - labelWidth).coerceAtLeast(0)
                            else -> (x - labelWidth / 2).coerceIn(0, (contentWidth - labelWidth).coerceAtLeast(0))
                        }
                        topMargin = 0.dp()
                    }
                }
                timelineRulerLayer.addView(label)
            }
        }
    }

    private fun renderNleSegments(
        segments: List<com.reallive.android.network.HistorySegmentDto>,
        thumbs: List<com.reallive.android.network.HistoryThumbnailDto>,
    ) {
        if (timelineEndMs <= timelineStartMs) return
        val widthPx = timelineWidthPx()
        if (widthPx <= 0) return
        val sortedThumbs = thumbs.sortedBy { it.ts }
        val totalMs = (timelineEndMs - timelineStartMs).coerceAtLeast(1L)
        val tileHeight = 38.dp()
        val targetTileWidthPx = when {
            timelineZoom >= 9.0f -> 30.dp()
            timelineZoom >= 8.0f -> 32.dp()
            timelineZoom >= 7.0f -> 34.dp()
            timelineZoom >= 6.0f -> 36.dp()
            timelineZoom >= 5.0f -> 40.dp()
            timelineZoom >= 4.0f -> 46.dp()
            timelineZoom >= 3.2f -> 52.dp()
            timelineZoom >= 2.4f -> 58.dp()
            timelineZoom >= 1.7f -> 64.dp()
            else -> 72.dp()
        }
        val minAspectWidthPx = (tileHeight * 4f / 3f).toInt().coerceAtLeast(40.dp())
        val effectiveTileWidthPx = targetTileWidthPx.coerceAtLeast(minAspectWidthPx)
        val tileCount = kotlin.math.max(1, kotlin.math.ceil(widthPx / effectiveTileWidthPx.toDouble()).toInt())
        val bucketMs = (totalMs / tileCount).coerceAtLeast(1L)
        val forcedBucketThumbs = buildForcedBucketThumbnails(segments, sortedThumbs, tileCount, bucketMs)

        for (i in 0 until tileCount) {
            val startX = (i * widthPx) / tileCount
            val endX = ((i + 1) * widthPx) / tileCount
            val tileWidth = (endX - startX).coerceAtLeast(2.dp())
            val bucketStartMs = timelineStartMs + i * bucketMs
            val bucketEndMs = if (i == tileCount - 1) timelineEndMs else (bucketStartMs + bucketMs)
            val bucketCenterMs = bucketStartMs + (bucketEndMs - bucketStartMs) / 2L
            val hasRecording = hasSegmentOverlap(segments, bucketStartMs, bucketEndMs)
            val block = FrameLayout(this).apply {
                layoutParams = FrameLayout.LayoutParams(tileWidth, tileHeight).apply {
                    leftMargin = startX.coerceAtLeast(0)
                    topMargin = 0
                }
                clipToOutline = true
            }
            val image = ImageView(this).apply {
                layoutParams = FrameLayout.LayoutParams(
                    FrameLayout.LayoutParams.MATCH_PARENT,
                    FrameLayout.LayoutParams.MATCH_PARENT,
                )
                scaleType = ImageView.ScaleType.CENTER_CROP
            }
            val thumb = forcedBucketThumbs[i] ?: nearestThumbnailForBucket(sortedThumbs, bucketStartMs, bucketEndMs)
            if (hasRecording && thumb != null) {
                image.load(resolveMediaUrl(thumb.url))
            } else {
                image.setImageDrawable(null)
                image.setBackgroundResource(R.drawable.bg_hist_thumb_empty)
            }
            block.addView(image)
            block.setOnClickListener {
                lifecycleScope.launch { loadPlayback(bucketCenterMs) }
            }
            timelineClipLayer.addView(block)
        }
    }

    private fun renderEventMarkers(events: List<HistoryTimelineEventDto>) {
        timelineMarkerLayer.removeAllViews()
        timelineMarkerLayer.post {
            val width = timelineWidthPx()
            if (width <= 0 || timelineEndMs <= timelineStartMs) return@post
            val markerSize = 14.dp()
            val dotSize = 10.dp()
            events.sortedBy { it.ts }.forEach { evt ->
                val x = tsToTrackX(evt.ts).coerceAtLeast(0)
                val marker = FrameLayout(this).apply {
                    layoutParams = FrameLayout.LayoutParams(markerSize, markerSize).apply {
                        leftMargin = (x - markerSize / 2).coerceAtLeast(0)
                        topMargin = 0
                    }
                    elevation = 4.dp().toFloat()
                }
                marker.addView(
                    View(this).apply {
                        layoutParams = FrameLayout.LayoutParams(
                            FrameLayout.LayoutParams.MATCH_PARENT,
                            FrameLayout.LayoutParams.MATCH_PARENT,
                        )
                        setBackgroundResource(R.drawable.bg_timeline_dot_halo)
                    },
                )
                marker.addView(
                    View(this).apply {
                        layoutParams = FrameLayout.LayoutParams(dotSize, dotSize).apply {
                            gravity = android.view.Gravity.CENTER
                        }
                        setBackgroundResource(
                            if (evt.type.lowercase(Locale.US).contains("person")) {
                                R.drawable.bg_timeline_dot_alert
                            } else if (evt.type.lowercase(Locale.US).contains("motion")) {
                                R.drawable.bg_timeline_dot_motion
                            } else {
                                R.drawable.bg_timeline_dot_default
                            },
                        )
                    },
                )
                marker.setOnClickListener {
                    lifecycleScope.launch { loadPlayback(evt.ts) }
                }
                timelineMarkerLayer.addView(marker)
            }
        }
    }

    private fun tsToProgress(ts: Long): Int {
        if (timelineEndMs <= timelineStartMs) return 0
        val ratio = ((ts - timelineStartMs).toDouble() / (timelineEndMs - timelineStartMs).toDouble())
            .coerceIn(0.0, 1.0)
        return (ratio * 1000.0).toInt()
    }

    private fun progressToTs(progress: Int): Long {
        if (timelineEndMs <= timelineStartMs) return selectedDayStartMs
        val ratio = (progress / 1000.0).coerceIn(0.0, 1.0)
        return timelineStartMs + ((timelineEndMs - timelineStartMs) * ratio).toLong()
    }

    private fun Int.dp(): Int = (this * resources.displayMetrics.density).toInt()

    private fun bindTimelineInteractions() {
        scaleDetector = ScaleGestureDetector(this, object : ScaleGestureDetector.SimpleOnScaleGestureListener() {
            override fun onScaleBegin(detector: ScaleGestureDetector): Boolean {
                suppressSeekOnNextUp = true
                return true
            }

            override fun onScale(detector: ScaleGestureDetector): Boolean {
                val old = timelineZoom
                val focusTs = xToTs(timelineScroll.scrollX + detector.focusX)
                timelineZoom = (timelineZoom * detector.scaleFactor).coerceIn(1.0f, 30.0f)
                if (kotlin.math.abs(timelineZoom - old) > 0.002f) {
                    pendingZoomFocusTs = focusTs
                    pendingZoomFocusX = detector.focusX
                    val now = SystemClock.uptimeMillis()
                    val zoomDelta = kotlin.math.abs(timelineZoom - lastRenderedZoom)
                    if (zoomDelta >= 0.04f || now - lastZoomRenderAtMs >= 33L) {
                        requestZoomRender(force = false)
                    }
                }
                return true
            }

            override fun onScaleEnd(detector: ScaleGestureDetector) {
                requestZoomRender(force = true)
            }
        })

        timelineScroll.setOnScrollChangeListener { _, _, _, _, _ ->
            updateTimelineTickLabels()
        }

        timelineScroll.setOnTouchListener { _, event ->
            scaleDetector.onTouchEvent(event)
            if (event.pointerCount >= 2) {
                isTimelineUserInteracting = true
                lastTimelineTouchUptimeMs = SystemClock.uptimeMillis()
                suppressSeekOnNextUp = true
                timelineScroll.requestDisallowInterceptTouchEvent(true)
                return@setOnTouchListener true
            }
            when (event.actionMasked) {
                MotionEvent.ACTION_DOWN, MotionEvent.ACTION_MOVE -> {
                    isTimelineUserInteracting = true
                    lastTimelineTouchUptimeMs = SystemClock.uptimeMillis()
                    false
                }
                MotionEvent.ACTION_UP -> {
                    isTimelineUserInteracting = false
                    lastTimelineTouchUptimeMs = SystemClock.uptimeMillis()
                    if (suppressSeekOnNextUp) {
                        suppressSeekOnNextUp = false
                        return@setOnTouchListener true
                    }
                    timelineScroll.post {
                        val child = timelineScroll.getChildAt(0)
                        val viewport = timelineScroll.width.coerceAtLeast(1)
                        val maxScroll = ((child?.width ?: 0) - viewport).coerceAtLeast(0)
                        val scrollX = timelineScroll.scrollX.coerceIn(0, maxScroll)
                        val edgeThreshold = 8.dp()
                        val ts = when {
                            maxScroll <= 0 -> timelineStartMs
                            scrollX >= maxScroll - edgeThreshold -> timelineEndMs
                            scrollX <= edgeThreshold -> timelineStartMs
                            else -> {
                                val x = scrollX + viewport / 2f
                                xToTs(x)
                            }
                        }
                        lifecycleScope.launch { loadPlayback(ts) }
                    }
                    false
                }
                MotionEvent.ACTION_CANCEL -> {
                    isTimelineUserInteracting = false
                    lastTimelineTouchUptimeMs = SystemClock.uptimeMillis()
                    suppressSeekOnNextUp = false
                    false
                }
                else -> false
            }
        }
    }

    private fun startPlaybackTicker() {
        if (playbackTickerActive) return
        playbackTickerActive = true
        lastPlaybackTickUptimeMs = SystemClock.uptimeMillis()
        timelineDynamic.removeCallbacks(playbackTickerRunnable)
        timelineDynamic.post(playbackTickerRunnable)
    }

    private fun stopPlaybackTicker() {
        playbackTickerActive = false
        currentPlaybackSegmentStartMs = -1L
        playbackNativeBasePosMs = -1L
        playbackNativeBaseTsMs = -1L
        playbackAnchorTsMs = -1L
        playbackAnchorUptimeMs = 0L
        timelineDynamic.removeCallbacks(playbackTickerRunnable)
    }

    private fun requestZoomRender(force: Boolean) {
        if (force) {
            timelineDynamic.removeCallbacks(zoomRenderRunnable)
            pendingZoomRender = false
            zoomRenderRunnable.run()
            return
        }
        if (pendingZoomRender) return
        val now = SystemClock.uptimeMillis()
        val delayMs = (16L - (now - lastZoomRenderAtMs)).coerceAtLeast(0L)
        pendingZoomRender = true
        timelineDynamic.postDelayed(zoomRenderRunnable, delayMs)
    }

    private fun hideVideoTimelineControls() {
        playerTimeRow.visibility = View.GONE
        playerProgressArea.visibility = View.GONE
        seekBar.visibility = View.GONE
    }

    private fun timelineWidthPx(): Int {
        if (baseTimelineWidthPx <= 0) baseTimelineWidthPx = resources.displayMetrics.widthPixels - 40.dp()
        return (baseTimelineWidthPx * timelineZoom).toInt().coerceAtLeast(baseTimelineWidthPx)
    }

    private fun timelineViewportHalfWidthPx(): Int {
        if (baseTimelineWidthPx <= 0) baseTimelineWidthPx = resources.displayMetrics.widthPixels - 40.dp()
        val viewport = when {
            timelineScroll.width > 0 -> timelineScroll.width
            timelineDynamic.width > 0 -> timelineDynamic.width
            else -> baseTimelineWidthPx
        }
        return (viewport / 2).coerceAtLeast(0)
    }

    private fun timelineViewportWidthPx(): Int {
        if (baseTimelineWidthPx <= 0) baseTimelineWidthPx = resources.displayMetrics.widthPixels - 40.dp()
        return when {
            timelineScroll.width > 0 -> timelineScroll.width
            timelineDynamic.width > 0 -> timelineDynamic.width
            else -> baseTimelineWidthPx
        }.coerceAtLeast(1)
    }

    private fun timelineRequiredSidePadPx(): Int {
        return timelineViewportWidthPx() / 2
    }

    private fun xToTs(xInContent: Float): Long {
        val width = timelineWidthPx().toFloat().coerceAtLeast(1f)
        val trackX = (xInContent - timelineSidePadPx.toFloat()).coerceIn(0f, width)
        val ratio = (trackX / width).coerceIn(0f, 1f)
        if (timelineEndMs <= timelineStartMs) return selectedDayStartMs
        return timelineStartMs + ((timelineEndMs - timelineStartMs) * ratio).toLong()
    }

    private fun syncTimelineToTs(ts: Long) {
        if (timelineEndMs <= timelineStartMs) return
        val x = tsToX(ts)
        val viewport = timelineViewportWidthPx()
        if (viewport > 0) {
            val targetScroll = (x - viewport / 2).coerceAtLeast(0)
            timelineScroll.scrollTo(targetScroll, 0)
            updateTimelineTickLabels()
        }
    }

    private fun updateTimelineTickLabels() {
        if (timelineEndMs <= timelineStartMs) return
        val viewportWidth = if (timelineScroll.width > 0) timelineScroll.width else timelineDynamic.width
        if (viewportWidth <= 0) return
        val startTs = xToTs(timelineScroll.scrollX.toFloat())
        val endTs = xToTs((timelineScroll.scrollX + viewportWidth).toFloat())
        val centerTs = xToTs((timelineScroll.scrollX + viewportWidth / 2f))
        val span = (endTs - startTs).coerceAtLeast(1L)
        timelineTick0.text = formatTickForAxis(startTs)
        timelineTick1.text = formatTickForAxis(startTs + span / 4L)
        timelineTick2.text = formatTickForAxis(startTs + span / 2L)
        timelineTick3.text = formatTickForAxis(startTs + span * 3L / 4L)
        timelineTick4.text = formatTickForAxis(endTs)
        playheadTimeText.text = formatPlayheadTime(centerTs)
    }

    private fun formatTick(ts: Long): String {
        return SimpleDateFormat("HH:mm", Locale.getDefault()).format(Date(ts))
    }

    private fun formatTickForAxis(ts: Long): String {
        val dayStart = selectedDayStartMs
        val dayEndExclusive = dayStart + 24 * 60 * 60 * 1000L
        return when {
            ts <= dayStart + 60_000L -> "00:00"
            ts >= dayEndExclusive - 60_000L -> "24:00"
            else -> formatTick(ts)
        }
    }

    private fun formatPlayheadTime(ts: Long): String {
        val dayStart = selectedDayStartMs
        val dayEndExclusive = dayStart + 24 * 60 * 60 * 1000L
        return when {
            ts <= dayStart + 1_000L -> "00:00:00"
            ts >= dayEndExclusive - 1_000L -> "24:00:00"
            else -> formatClock(ts)
        }
    }

    private fun tsToX(ts: Long): Int {
        if (timelineEndMs <= timelineStartMs) return 0
        val ratio = ((ts - timelineStartMs).toDouble() / (timelineEndMs - timelineStartMs).toDouble()).coerceIn(0.0, 1.0)
        return timelineSidePadPx + (ratio * timelineWidthPx()).toInt()
    }

    private fun tsToTrackX(ts: Long): Int {
        if (timelineEndMs <= timelineStartMs) return 0
        val ratio = ((ts - timelineStartMs).toDouble() / (timelineEndMs - timelineStartMs).toDouble()).coerceIn(0.0, 1.0)
        return (ratio * timelineWidthPx()).toInt()
    }

    private fun buildForcedBucketThumbnails(
        segments: List<com.reallive.android.network.HistorySegmentDto>,
        thumbs: List<com.reallive.android.network.HistoryThumbnailDto>,
        tileCount: Int,
        bucketMs: Long,
    ): Map<Int, com.reallive.android.network.HistoryThumbnailDto> {
        if (tileCount <= 0 || bucketMs <= 0L || thumbs.isEmpty()) return emptyMap()
        val out = mutableMapOf<Int, com.reallive.android.network.HistoryThumbnailDto>()
        segments.forEach { seg ->
            val start = seg.startMs.coerceIn(timelineStartMs, timelineEndMs)
            val end = seg.endMs.coerceIn(timelineStartMs, timelineEndMs)
            if (end <= start) return@forEach
            val center = start + (end - start) / 2L
            val index = (((center - timelineStartMs) / bucketMs).toInt()).coerceIn(0, tileCount - 1)
            val thumb = nearestThumbnailInRange(thumbs, start, end)
                ?: nearestThumbnailByTs(thumbs, center)
            if (thumb != null && out[index] == null) {
                out[index] = thumb
            }
        }
        return out
    }

    private fun hasSegmentOverlap(
        segments: List<com.reallive.android.network.HistorySegmentDto>,
        bucketStartMs: Long,
        bucketEndMs: Long,
    ): Boolean {
        return segments.any { seg ->
            seg.endMs > bucketStartMs && seg.startMs < bucketEndMs
        }
    }

    private fun nearestThumbnailInRange(
        thumbs: List<com.reallive.android.network.HistoryThumbnailDto>,
        startMs: Long,
        endMs: Long,
    ): com.reallive.android.network.HistoryThumbnailDto? {
        if (thumbs.isEmpty()) return null
        val center = startMs + (endMs - startMs) / 2L
        var best: com.reallive.android.network.HistoryThumbnailDto? = null
        var bestDist = Long.MAX_VALUE
        thumbs.forEach { t ->
            if (t.ts in startMs..endMs) {
                val d = kotlin.math.abs(t.ts - center)
                if (d < bestDist) {
                    best = t
                    bestDist = d
                }
            }
        }
        return best
    }

    private fun nearestThumbnailByTs(
        thumbs: List<com.reallive.android.network.HistoryThumbnailDto>,
        ts: Long,
    ): com.reallive.android.network.HistoryThumbnailDto? {
        if (thumbs.isEmpty()) return null
        var best: com.reallive.android.network.HistoryThumbnailDto? = null
        var bestDist = Long.MAX_VALUE
        thumbs.forEach { t ->
            val d = kotlin.math.abs(t.ts - ts)
            if (d < bestDist) {
                best = t
                bestDist = d
            }
        }
        return best
    }

    private fun nearestThumbnailForBucket(
        thumbs: List<com.reallive.android.network.HistoryThumbnailDto>,
        bucketStartMs: Long,
        bucketEndMs: Long,
    ): com.reallive.android.network.HistoryThumbnailDto? {
        if (thumbs.isEmpty()) return null
        val center = bucketStartMs + (bucketEndMs - bucketStartMs) / 2L
        val maxDist = kotlin.math.max(90_000L, (bucketEndMs - bucketStartMs))
        var best: com.reallive.android.network.HistoryThumbnailDto? = null
        var bestDist = Long.MAX_VALUE
        thumbs.forEach { t ->
            val d = kotlin.math.abs(t.ts - center)
            if (d < bestDist && d <= maxDist) {
                best = t
                bestDist = d
            }
        }
        return best
    }

    companion object {
        private val SPEED_LABELS = listOf("0.5x", "1x", "2x")
        const val EXTRA_SELECTED_DAY_START_MS = "extra_selected_day_start_ms"
    }
}
