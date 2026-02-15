package com.reallive.android.ui.watch

import android.os.Bundle
import android.widget.Button
import android.widget.SeekBar
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import androidx.lifecycle.lifecycleScope
import androidx.recyclerview.widget.LinearLayoutManager
import androidx.recyclerview.widget.RecyclerView
import com.reallive.android.R
import com.reallive.android.config.AppConfig
import com.reallive.android.data.CameraRepository
import com.reallive.android.network.ApiClient
import com.reallive.android.network.HistoryTimelineDto
import com.reallive.android.network.StreamInfoDto
import com.reallive.android.watch.WatchSessionManager
import com.reallive.player.Player
import com.reallive.player.PlayerFactory
import com.reallive.player.PlayerSurfaceView
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.net.URLEncoder
import java.nio.charset.StandardCharsets
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

class WatchActivity : AppCompatActivity() {
    private lateinit var repository: CameraRepository
    private lateinit var player: Player
    private lateinit var surfaceView: PlayerSurfaceView
    private lateinit var watchSession: WatchSessionManager

    private lateinit var titleText: TextView
    private lateinit var statusText: TextView
    private lateinit var timelineSeek: SeekBar
    private lateinit var timelineStartText: TextView
    private lateinit var timelineEndText: TextView
    private lateinit var timelineSelectedText: TextView

    private lateinit var thumbAdapter: TimelineThumbnailAdapter
    private lateinit var eventAdapter: TimelineEventAdapter

    private var refreshJob: Job? = null
    private val seekSteps = 1000
    private var isLiveMode = true

    private var cameraId: Long = -1
    private var cameraName: String = "Camera"
    private var streamKey: String = ""
    private var baseUrl: String = ""
    private var token: String? = null

    private var timelineStartMs: Long = 0L
    private var timelineEndMs: Long = 0L
    private var selectedTsMs: Long = 0L

    private val timeFormat = SimpleDateFormat("yyyy-MM-dd HH:mm:ss", Locale.getDefault())

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_watch)

        cameraId = intent.getLongExtra(EXTRA_CAMERA_ID, -1L)
        cameraName = intent.getStringExtra(EXTRA_CAMERA_NAME) ?: "Camera"
        streamKey = intent.getStringExtra(EXTRA_STREAM_KEY) ?: ""
        baseUrl = intent.getStringExtra(EXTRA_BASE_URL) ?: AppConfig(this).getBaseUrl()
        token = intent.getStringExtra(EXTRA_TOKEN)
        if (cameraId <= 0L) {
            finish()
            return
        }

        titleText = findViewById(R.id.watch_title)
        statusText = findViewById(R.id.watch_status)
        timelineSeek = findViewById(R.id.timeline_seek)
        timelineStartText = findViewById(R.id.timeline_start)
        timelineEndText = findViewById(R.id.timeline_end)
        timelineSelectedText = findViewById(R.id.timeline_selected)

        titleText.text = cameraName
        timelineSeek.max = seekSteps

        val appConfig = AppConfig(this)
        if (token.isNullOrBlank()) {
            token = appConfig.getToken()
        }
        if (baseUrl.isBlank()) {
            baseUrl = appConfig.getBaseUrl()
        }

        val api = ApiClient.create(baseUrl) { token }
        repository = CameraRepository(api)
        watchSession = WatchSessionManager(api, cameraId)

        player = PlayerFactory.create()
        surfaceView = findViewById(R.id.watch_surface)
        surfaceView.bindPlayer(player)

        setupTimelineViews()
        setupActions()
    }

    override fun onStart() {
        super.onStart()
        refreshJob?.cancel()
        refreshJob = lifecycleScope.launch(Dispatchers.IO) {
            runCatching { watchSession.start() }
            val streamInfo = runCatching { repository.getStreamInfo(cameraId) }.getOrNull()
            if (!streamInfo?.stream_key.isNullOrBlank()) {
                streamKey = streamInfo?.stream_key ?: streamKey
            }
            withContext(Dispatchers.Main) {
                playLive()
            }
            refreshHistoryAndUi(streamInfo)

            while (isActive) {
                delay(10_000)
                val info = runCatching { repository.getStreamInfo(cameraId) }.getOrNull()
                refreshHistoryAndUi(info)
            }
        }
    }

    override fun onStop() {
        refreshJob?.cancel()
        refreshJob = null
        lifecycleScope.launch(Dispatchers.IO) {
            runCatching { watchSession.stop() }
        }
        runCatching { player.stop() }
        super.onStop()
    }

    override fun onDestroy() {
        surfaceView.unbindPlayer()
        player.release()
        super.onDestroy()
    }

    private fun setupTimelineViews() {
        val thumbRecycler = findViewById<RecyclerView>(R.id.timeline_thumbnail_recycler)
        thumbAdapter = TimelineThumbnailAdapter { tsMs ->
            selectedTsMs = tsMs
            syncSeekProgressFromTs()
            updateTimelineLabels()
            seekToSelectedTimestamp()
        }
        thumbRecycler.layoutManager = LinearLayoutManager(this, RecyclerView.HORIZONTAL, false)
        thumbRecycler.adapter = thumbAdapter

        val eventRecycler = findViewById<RecyclerView>(R.id.timeline_event_recycler)
        eventAdapter = TimelineEventAdapter { evt ->
            selectedTsMs = evt.tsMs
            syncSeekProgressFromTs()
            updateTimelineLabels()
            seekToSelectedTimestamp()
        }
        eventRecycler.layoutManager = LinearLayoutManager(this)
        eventRecycler.adapter = eventAdapter

        timelineSeek.setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
            override fun onProgressChanged(seekBar: SeekBar?, progress: Int, fromUser: Boolean) {
                if (!fromUser) return
                selectedTsMs = tsFromProgress(progress)
                updateTimelineLabels()
            }

            override fun onStartTrackingTouch(seekBar: SeekBar?) = Unit

            override fun onStopTrackingTouch(seekBar: SeekBar?) {
                selectedTsMs = tsFromProgress(seekBar?.progress ?: 0)
                updateTimelineLabels()
            }
        })
    }

    private fun setupActions() {
        findViewById<Button>(R.id.btn_back_live).setOnClickListener {
            playLive()
        }
        findViewById<Button>(R.id.btn_seek_selected).setOnClickListener {
            seekToSelectedTimestamp()
        }
    }

    private suspend fun refreshHistoryAndUi(streamInfo: StreamInfoDto?) {
        runCatching {
            val overview = repository.getHistoryOverview(cameraId)
            val range = overview.timeRange
            if (range == null || !overview.hasHistory) {
                withContext(Dispatchers.Main) {
                    statusText.text = buildStatusText(streamInfo, "No history")
                    timelineStartMs = 0L
                    timelineEndMs = 0L
                    selectedTsMs = 0L
                    timelineSeek.progress = 0
                    timelineStartText.text = "--"
                    timelineEndText.text = "--"
                    timelineSelectedText.text = "--"
                    thumbAdapter.submitList(emptyList())
                    eventAdapter.submitList(emptyList())
                }
                return
            }

            val timeline = repository.getTimeline(cameraId, range.startMs, range.endMs)
            withContext(Dispatchers.Main) {
                applyTimelineData(timeline)
                val historyMsg = "History ${overview.segmentCount} segments, ${overview.eventCount ?: 0} events"
                statusText.text = buildStatusText(streamInfo, historyMsg)
            }
        }.onFailure { err ->
            lifecycleScope.launch(Dispatchers.Main) {
                statusText.text = "Load failed: ${err.message ?: "unknown"}"
            }
        }
    }

    private fun applyTimelineData(timeline: HistoryTimelineDto) {
        val start = timeline.startMs ?: 0L
        val end = timeline.endMs ?: start

        timelineStartMs = start
        timelineEndMs = if (end >= start) end else start

        if (selectedTsMs !in timelineStartMs..timelineEndMs) {
            selectedTsMs = timelineEndMs
        }

        val thumbnails = if (timeline.thumbnails.isNotEmpty()) {
            timeline.thumbnails.mapNotNull { item ->
                val abs = absoluteUrl(item.url) ?: return@mapNotNull null
                TimelineThumbnailItem(item.ts, abs)
            }
        } else {
            timeline.segments.mapNotNull { seg ->
                val ts = (seg.startMs + seg.endMs) / 2
                val thumb = absoluteUrl(seg.thumbnailUrl) ?: return@mapNotNull null
                TimelineThumbnailItem(ts, thumb)
            }
        }
        thumbAdapter.submitList(thumbnails)

        val events = timeline.events
            .filter { it.type.equals("person-detected", ignoreCase = true) || it.type.equals("person", ignoreCase = true) }
            .map {
                TimelineEventItem(
                    tsMs = it.ts,
                    type = it.type,
                    score = it.score ?: 0.0,
                )
            }
            .sortedByDescending { it.tsMs }
        eventAdapter.submitList(events)

        syncSeekProgressFromTs()
        updateTimelineLabels()
    }

    private fun seekToSelectedTimestamp() {
        if (cameraId <= 0 || selectedTsMs <= 0) return
        lifecycleScope.launch(Dispatchers.IO) {
            val playback = runCatching { repository.getHistoryPlayback(cameraId, selectedTsMs) }.getOrNull()
            val playbackUrl = absoluteUrl(playback?.playbackUrl)
            if (playback?.mode == "history" && !playbackUrl.isNullOrBlank()) {
                val offsetMs = playback.offsetSec.coerceAtLeast(0) * 1000L
                withContext(Dispatchers.Main) {
                    isLiveMode = false
                    player.playHistory(playbackUrl, offsetMs)
                    statusText.text = "History @ ${timeFormat.format(Date(selectedTsMs))}"
                }
            } else {
                withContext(Dispatchers.Main) {
                    playLive()
                }
            }
        }
    }

    private fun playLive() {
        if (streamKey.isBlank()) return
        isLiveMode = true
        val encoded = URLEncoder.encode(streamKey, StandardCharsets.UTF_8.toString())
        val liveUrl = "${baseUrl}live/$encoded.flv"
        player.playLive(liveUrl)
        updateTimelineLabels()
        statusText.text = "Live"
    }

    private fun syncSeekProgressFromTs() {
        val span = (timelineEndMs - timelineStartMs).coerceAtLeast(1L)
        val safeTs = selectedTsMs.coerceIn(timelineStartMs, timelineEndMs)
        val ratio = (safeTs - timelineStartMs).toDouble() / span.toDouble()
        timelineSeek.progress = (ratio * seekSteps).toInt().coerceIn(0, seekSteps)
    }

    private fun tsFromProgress(progress: Int): Long {
        val span = (timelineEndMs - timelineStartMs).coerceAtLeast(1L)
        val ratio = progress.toDouble() / seekSteps.toDouble()
        return timelineStartMs + (span.toDouble() * ratio).toLong()
    }

    private fun updateTimelineLabels() {
        timelineStartText.text = if (timelineStartMs > 0) timeFormat.format(Date(timelineStartMs)) else "--"
        timelineEndText.text = if (timelineEndMs > 0) timeFormat.format(Date(timelineEndMs)) else "--"
        val suffix = if (isLiveMode) " (live)" else ""
        timelineSelectedText.text = if (selectedTsMs > 0) {
            timeFormat.format(Date(selectedTsMs)) + suffix
        } else {
            "--"
        }
    }

    private fun buildStatusText(streamInfo: StreamInfoDto?, history: String): String {
        val streamStatus = streamInfo?.status ?: "unknown"
        val device = streamInfo?.device
        val online = if (device?.running == true) "online" else "offline"
        return "$streamStatus | device=$online | $history"
    }

    private fun absoluteUrl(path: String?): String? {
        if (path.isNullOrBlank()) return null
        if (path.startsWith("http://") || path.startsWith("https://")) return path
        val root = if (baseUrl.endsWith('/')) baseUrl.dropLast(1) else baseUrl
        val rel = if (path.startsWith('/')) path else "/$path"
        return "$root$rel"
    }

    companion object {
        const val EXTRA_CAMERA_ID = "extra_camera_id"
        const val EXTRA_CAMERA_NAME = "extra_camera_name"
        const val EXTRA_STREAM_KEY = "extra_stream_key"
        const val EXTRA_BASE_URL = "extra_base_url"
        const val EXTRA_TOKEN = "extra_token"
    }
}
