package com.reallive.android.ui.watch.ptz

import android.os.Bundle
import android.widget.Button
import android.widget.SeekBar
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import com.google.android.material.appbar.MaterialToolbar
import com.reallive.android.R

class PtzActivity : AppCompatActivity() {
    private lateinit var statusText: TextView
    private lateinit var speedText: TextView
    private var cameraName: String = "Camera"
    private var speedValue: Int = 3

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_ptz)

        cameraName = intent.getStringExtra(EXTRA_CAMERA_NAME) ?: "Camera"
        statusText = findViewById(R.id.ptz_status)
        speedText = findViewById(R.id.ptz_speed)

        findViewById<MaterialToolbar>(R.id.ptz_toolbar).apply {
            subtitle = cameraName
            setNavigationOnClickListener { finish() }
        }

        findViewById<Button>(R.id.ptz_up).setOnClickListener { onPtzAction("Tilt up") }
        findViewById<Button>(R.id.ptz_down).setOnClickListener { onPtzAction("Tilt down") }
        findViewById<Button>(R.id.ptz_left).setOnClickListener { onPtzAction("Pan left") }
        findViewById<Button>(R.id.ptz_right).setOnClickListener { onPtzAction("Pan right") }
        findViewById<Button>(R.id.ptz_stop).setOnClickListener { onPtzAction("Stop") }

        findViewById<SeekBar>(R.id.ptz_speed_seek).apply {
            progress = speedValue
            setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
                override fun onProgressChanged(seekBar: SeekBar?, progress: Int, fromUser: Boolean) {
                    speedValue = progress
                    updateSpeedLabel()
                }

                override fun onStartTrackingTouch(seekBar: SeekBar?) = Unit
                override fun onStopTrackingTouch(seekBar: SeekBar?) = Unit
            })
        }
        updateSpeedLabel()
        statusText.text = "Ready"
    }

    private fun updateSpeedLabel() {
        speedText.text = "Speed: ${speedValue + 1}"
    }

    private fun onPtzAction(action: String) {
        statusText.text = "$action · speed ${speedValue + 1} (mock)"
    }

    companion object {
        const val EXTRA_CAMERA_ID = "extra_camera_id"
        const val EXTRA_CAMERA_NAME = "extra_camera_name"
    }
}
