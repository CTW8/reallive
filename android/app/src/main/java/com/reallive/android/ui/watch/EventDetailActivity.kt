package com.reallive.android.ui.watch

import android.os.Bundle
import android.widget.TextView
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import com.google.android.material.appbar.MaterialToolbar
import com.google.android.material.button.MaterialButton
import com.reallive.android.R
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

class EventDetailActivity : AppCompatActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_event_detail)
        findViewById<MaterialToolbar>(R.id.event_detail_toolbar).setNavigationOnClickListener { finish() }

        val type = intent.getStringExtra(EXTRA_EVENT_TYPE) ?: "event"
        val ts = intent.getLongExtra(EXTRA_EVENT_TS, System.currentTimeMillis())
        val score = intent.getDoubleExtra(EXTRA_EVENT_SCORE, 0.0)
        val cameraName = intent.getStringExtra(EXTRA_CAMERA_NAME) ?: "Camera"

        val time = SimpleDateFormat("yyyy-MM-dd HH:mm:ss", Locale.getDefault()).format(Date(ts))
        findViewById<TextView>(R.id.event_detail_title).text = type.replace('-', ' ')
        findViewById<TextView>(R.id.event_detail_meta).text = "$time • ${cameraName}"
        findViewById<TextView>(R.id.event_detail_score).text = if (score > 0.0) {
            "Confidence ${(score * 100.0).toInt()}%"
        } else {
            "Confidence -"
        }
        findViewById<MaterialButton>(R.id.event_detail_download).setOnClickListener {
            Toast.makeText(this, "Download clip (mock)", Toast.LENGTH_SHORT).show()
        }
        findViewById<MaterialButton>(R.id.event_detail_share).setOnClickListener {
            Toast.makeText(this, "Share clip (mock)", Toast.LENGTH_SHORT).show()
        }
    }

    companion object {
        const val EXTRA_EVENT_TYPE = "extra_event_type"
        const val EXTRA_EVENT_TS = "extra_event_ts"
        const val EXTRA_EVENT_SCORE = "extra_event_score"
        const val EXTRA_CAMERA_NAME = "extra_camera_name"
    }
}
