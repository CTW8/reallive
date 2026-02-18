package com.reallive.android.ui.camera

import android.content.Intent
import android.os.Bundle
import androidx.appcompat.app.AppCompatActivity
import com.google.android.material.appbar.MaterialToolbar
import com.google.android.material.button.MaterialButton
import com.reallive.android.R

class CameraConnectProgressActivity : AppCompatActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_camera_connect_progress)
        findViewById<MaterialToolbar>(R.id.camera_connect_progress_toolbar).setNavigationOnClickListener { finish() }
        findViewById<MaterialButton>(R.id.camera_connect_to_success).setOnClickListener {
            startActivity(Intent(this, CameraConnectSuccessActivity::class.java))
            finish()
        }
        findViewById<MaterialButton>(R.id.camera_connect_to_failed).setOnClickListener {
            startActivity(Intent(this, CameraConnectFailedActivity::class.java))
            finish()
        }
    }
}
