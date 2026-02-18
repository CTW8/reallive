package com.reallive.android.ui.watch

import android.content.Intent
import android.os.Bundle
import android.widget.TextView
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import androidx.recyclerview.widget.LinearLayoutManager
import androidx.recyclerview.widget.RecyclerView
import com.google.android.material.appbar.MaterialToolbar
import com.google.android.material.button.MaterialButton
import com.reallive.android.R
import com.reallive.android.ui.camera.CameraSettingsActivity
import com.reallive.android.ui.history.HistoryActivity
import com.reallive.android.ui.notifications.NotificationsActivity
import com.reallive.android.ui.watch.ptz.PtzActivity
import com.reallive.android.ui.watch.snapshot.SnapshotGalleryActivity

class WatchActivity : AppCompatActivity() {
    private lateinit var titleText: TextView
    private lateinit var statusText: TextView
    private lateinit var eventAdapter: TimelineEventAdapter

    private var cameraId: Long = -1
    private var cameraName: String = "Camera"
    private var streamKey: String = ""

    private val events by lazy {
        listOf(
            TimelineEventItem(tsMs = System.currentTimeMillis() - 2 * 60_000L, type = "person-detected", score = 0.92),
            TimelineEventItem(tsMs = System.currentTimeMillis() - 15 * 60_000L, type = "motion", score = 0.74),
            TimelineEventItem(tsMs = System.currentTimeMillis() - 36 * 60_000L, type = "person-detected", score = 0.88),
            TimelineEventItem(tsMs = System.currentTimeMillis() - 66 * 60_000L, type = "stream-start", score = 0.0),
        )
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_watch)

        cameraId = intent.getLongExtra(EXTRA_CAMERA_ID, 1L)
        cameraName = intent.getStringExtra(EXTRA_CAMERA_NAME) ?: "Front Door"
        streamKey = intent.getStringExtra(EXTRA_STREAM_KEY) ?: "front-door"

        titleText = findViewById(R.id.watch_title)
        statusText = findViewById(R.id.watch_status)

        findViewById<MaterialToolbar>(R.id.watch_toolbar).setNavigationOnClickListener { finish() }
        titleText.text = cameraName
        statusText.text = "Live · 1080p · 30fps"

        setupActions()
        setupEventList()
    }

    private fun setupActions() {
        findViewById<MaterialButton>(R.id.btn_watch_playback).setOnClickListener {
            startActivity(
                Intent(this, HistoryActivity::class.java).apply {
                    putExtra(WatchActivity.EXTRA_CAMERA_ID, cameraId)
                    putExtra(WatchActivity.EXTRA_CAMERA_NAME, cameraName)
                    putExtra(WatchActivity.EXTRA_STREAM_KEY, streamKey)
                },
            )
        }
        findViewById<MaterialButton>(R.id.btn_watch_capture).setOnClickListener {
            Toast.makeText(this, getString(R.string.watch_snapshot_saved), Toast.LENGTH_SHORT).show()
        }
        findViewById<MaterialButton>(R.id.btn_watch_record).setOnClickListener {
            Toast.makeText(this, "Recording started (mock)", Toast.LENGTH_SHORT).show()
        }
        findViewById<MaterialButton>(R.id.btn_watch_share).setOnClickListener {
            startActivity(
                Intent(this, ShareSheetActivity::class.java).apply {
                    putExtra(ShareSheetActivity.EXTRA_CAMERA_NAME, cameraName)
                },
            )
        }
        findViewById<MaterialButton>(R.id.btn_watch_alerts).setOnClickListener {
            startActivity(Intent(this, NotificationsActivity::class.java))
        }
        findViewById<MaterialButton>(R.id.btn_watch_settings).setOnClickListener {
            startActivity(
                Intent(this, CameraSettingsActivity::class.java).apply {
                    putExtra(CameraSettingsActivity.EXTRA_CAMERA_ID, cameraId)
                    putExtra(CameraSettingsActivity.EXTRA_CAMERA_NAME, cameraName)
                },
            )
        }
        findViewById<MaterialButton>(R.id.btn_watch_ptz).setOnClickListener {
            startActivity(
                Intent(this, PtzActivity::class.java).apply {
                    putExtra(PtzActivity.EXTRA_CAMERA_ID, cameraId)
                    putExtra(PtzActivity.EXTRA_CAMERA_NAME, cameraName)
                },
            )
        }
        findViewById<MaterialButton>(R.id.btn_watch_download).setOnClickListener {
            Toast.makeText(this, "Download clip (mock)", Toast.LENGTH_SHORT).show()
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
        eventAdapter.submitList(events)
    }

    private fun openEventDetail(evt: TimelineEventItem) {
        startActivity(
            Intent(this, EventDetailActivity::class.java).apply {
                putExtra(EventDetailActivity.EXTRA_EVENT_TYPE, evt.type)
                putExtra(EventDetailActivity.EXTRA_EVENT_TS, evt.tsMs)
                putExtra(EventDetailActivity.EXTRA_EVENT_SCORE, evt.score)
                putExtra(EventDetailActivity.EXTRA_CAMERA_NAME, cameraName)
            },
        )
    }

    companion object {
        const val EXTRA_CAMERA_ID = "extra_camera_id"
        const val EXTRA_CAMERA_NAME = "extra_camera_name"
        const val EXTRA_STREAM_KEY = "extra_stream_key"
    }
}
