package com.reallive.android.ui.camera

import android.content.Intent
import android.os.Bundle
import androidx.appcompat.app.AppCompatActivity
import com.reallive.android.R
import com.reallive.android.ui.dashboard.DashboardActivity
import com.reallive.android.ui.watch.WatchActivity

class CameraConnectSuccessActivity : AppCompatActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_camera_connect_success)
        findViewById<android.view.View>(R.id.camera_connect_success_back).setOnClickListener { finish() }
        findViewById<android.view.View>(R.id.camera_success_open_live).setOnClickListener {
            startActivity(Intent(this, WatchActivity::class.java))
            finish()
        }
        findViewById<android.view.View>(R.id.camera_success_back_list).setOnClickListener {
            startActivity(Intent(this, DashboardActivity::class.java))
            finish()
        }
    }
}
