package com.reallive.android.ui.camera

import android.content.Intent
import android.os.Bundle
import androidx.appcompat.app.AppCompatActivity
import com.reallive.android.R

class CameraConnectProgressActivity : AppCompatActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_camera_connect_progress)
        findViewById<android.view.View>(R.id.camera_connect_progress_back).setOnClickListener { finish() }
        findViewById<android.view.View>(R.id.camera_connect_progress_card).setOnClickListener {
            startActivity(Intent(this, CameraConnectSuccessActivity::class.java))
            finish()
        }
        findViewById<android.view.View>(R.id.camera_connect_cancel).setOnClickListener {
            startActivity(Intent(this, CameraConnectFailedActivity::class.java))
            finish()
        }
    }
}
