package com.reallive.android.ui.watch.ptz

import android.os.Bundle
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import com.reallive.android.R

class PtzActivity : AppCompatActivity() {
    companion object {
        const val EXTRA_CAMERA_ID = "extra_camera_id"
        const val EXTRA_CAMERA_NAME = "extra_camera_name"
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_ptz)

        findViewById<android.view.View>(R.id.ptz_back).setOnClickListener { finish() }
        findViewById<android.view.View>(R.id.ptz_more).setOnClickListener { finish() }

        findViewById<android.view.View>(R.id.ptz_up).setOnClickListener { onPtzAction("Tilt up") }
        findViewById<android.view.View>(R.id.ptz_down).setOnClickListener { onPtzAction("Tilt down") }
        findViewById<android.view.View>(R.id.ptz_left).setOnClickListener { onPtzAction("Pan left") }
        findViewById<android.view.View>(R.id.ptz_right).setOnClickListener { onPtzAction("Pan right") }
        findViewById<android.view.View>(R.id.ptz_stop).setOnClickListener { onPtzAction("Stop") }
    }

    private fun onPtzAction(action: String) {
        Toast.makeText(this, action, Toast.LENGTH_SHORT).show()
    }
}
