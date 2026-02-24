package com.reallive.android.ui.camera

import android.content.Intent
import android.os.Bundle
import androidx.appcompat.app.AppCompatActivity
import com.reallive.android.R
import com.reallive.android.ui.settings.PermissionGuideActivity

class CameraConnectFailedActivity : AppCompatActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_camera_connect_failed)
        findViewById<android.view.View>(R.id.camera_failed_back).setOnClickListener { finish() }
        findViewById<android.view.View>(R.id.camera_failed_retry).setOnClickListener {
            startActivity(
                Intent(this, CameraConnectProgressActivity::class.java).apply {
                    val name = intent.getStringExtra(CameraConnectProgressActivity.EXTRA_CAMERA_NAME)
                    val resolution = intent.getStringExtra(CameraConnectProgressActivity.EXTRA_CAMERA_RESOLUTION)
                    val source = intent.getStringExtra(CameraConnectProgressActivity.EXTRA_SETUP_SOURCE)
                    val streamKey = intent.getStringExtra(CameraConnectProgressActivity.EXTRA_STREAM_KEY)
                    if (!name.isNullOrBlank()) putExtra(CameraConnectProgressActivity.EXTRA_CAMERA_NAME, name)
                    if (!resolution.isNullOrBlank()) putExtra(CameraConnectProgressActivity.EXTRA_CAMERA_RESOLUTION, resolution)
                    if (!source.isNullOrBlank()) putExtra(CameraConnectProgressActivity.EXTRA_SETUP_SOURCE, source)
                    if (!streamKey.isNullOrBlank()) putExtra(CameraConnectProgressActivity.EXTRA_STREAM_KEY, streamKey)
                },
            )
            finish()
        }
        findViewById<android.view.View>(R.id.camera_failed_troubleshooting).setOnClickListener {
            startActivity(Intent(this, PermissionGuideActivity::class.java))
        }
    }
}
