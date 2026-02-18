package com.reallive.android.ui.camera

import android.content.Intent
import android.os.Bundle
import androidx.appcompat.app.AppCompatActivity
import com.google.android.material.appbar.MaterialToolbar
import com.google.android.material.button.MaterialButton
import com.reallive.android.R

class CameraSetupActivity : AppCompatActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_camera_setup)

        findViewById<MaterialToolbar>(R.id.camera_setup_toolbar).setNavigationOnClickListener { finish() }
        findViewById<MaterialButton>(R.id.camera_setup_connect).setOnClickListener {
            startActivity(Intent(this, CameraConnectProgressActivity::class.java))
        }
    }
}
