package com.reallive.android.ui.camera

import android.content.Intent
import android.os.Bundle
import androidx.appcompat.app.AppCompatActivity
import com.reallive.android.R
import com.reallive.android.ui.dashboard.DashboardActivity
import com.reallive.android.ui.watch.WatchActivity

class CameraConnectSuccessActivity : AppCompatActivity() {
    private var cameraId: Long = -1L
    private var cameraName: String = ""
    private var streamKey: String = ""
    private var setupSource: String = "manual"

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_camera_connect_success)
        cameraId = intent.getLongExtra(EXTRA_CAMERA_ID, -1L)
        cameraName = intent.getStringExtra(EXTRA_CAMERA_NAME).orEmpty()
        streamKey = intent.getStringExtra(EXTRA_STREAM_KEY).orEmpty()
        setupSource = intent.getStringExtra(EXTRA_SETUP_SOURCE).orEmpty().ifBlank { "manual" }
        findViewById<android.view.View>(R.id.camera_connect_success_back).setOnClickListener { finish() }
        findViewById<android.view.View>(R.id.camera_success_open_live).setOnClickListener {
            startActivity(
                Intent(this, WatchActivity::class.java).apply {
                    if (cameraId > 0L) putExtra(WatchActivity.EXTRA_CAMERA_ID, cameraId)
                    if (cameraName.isNotBlank()) putExtra(WatchActivity.EXTRA_CAMERA_NAME, cameraName)
                    if (streamKey.isNotBlank()) putExtra(WatchActivity.EXTRA_STREAM_KEY, streamKey)
                    putExtra(WatchActivity.EXTRA_ADD_SOURCE, setupSource)
                },
            )
            finish()
        }
        findViewById<android.view.View>(R.id.camera_success_back_list).setOnClickListener {
            startActivity(Intent(this, DashboardActivity::class.java))
            finish()
        }
    }

    companion object {
        const val EXTRA_CAMERA_ID = "extra_camera_id"
        const val EXTRA_CAMERA_NAME = "extra_camera_name"
        const val EXTRA_STREAM_KEY = "extra_stream_key"
        const val EXTRA_CAMERA_RESOLUTION = "extra_camera_resolution"
        const val EXTRA_SETUP_SOURCE = "extra_setup_source"
    }
}
