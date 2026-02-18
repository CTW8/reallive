package com.reallive.android.ui.camera

import android.content.Intent
import android.os.Bundle
import androidx.appcompat.app.AppCompatActivity
import com.google.android.material.button.MaterialButton
import com.reallive.android.R
import com.reallive.android.ui.settings.PermissionGuideActivity

class CameraConnectFailedActivity : AppCompatActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_camera_connect_failed)
        findViewById<MaterialButton>(R.id.camera_failed_retry).setOnClickListener {
            startActivity(Intent(this, CameraSetupActivity::class.java))
            finish()
        }
        findViewById<MaterialButton>(R.id.camera_failed_troubleshooting).setOnClickListener {
            startActivity(Intent(this, PermissionGuideActivity::class.java))
        }
    }
}
