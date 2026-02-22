package com.reallive.android.ui.camera

import android.content.Intent
import android.os.Bundle
import android.view.View
import androidx.appcompat.app.AppCompatActivity
import com.reallive.android.R

class CameraSetupActivity : AppCompatActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_camera_setup)

        findViewById<View>(R.id.camera_setup_back).setOnClickListener { finish() }
        findViewById<View>(R.id.camera_setup_connect).setOnClickListener {
            startActivity(Intent(this, CameraConnectProgressActivity::class.java))
        }
    }
}
