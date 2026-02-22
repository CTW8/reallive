package com.reallive.android.ui.watch.ptz

import android.annotation.SuppressLint
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.view.MotionEvent
import android.widget.SeekBar
import android.widget.TextView
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import androidx.lifecycle.lifecycleScope
import com.reallive.android.config.AppConfig
import com.reallive.android.data.CameraRepository
import com.reallive.android.R
import com.reallive.android.network.ApiClient
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

class PtzActivity : AppCompatActivity() {
    companion object {
        const val EXTRA_CAMERA_ID = "extra_camera_id"
        const val EXTRA_CAMERA_NAME = "extra_camera_name"
    }

    private lateinit var repository: CameraRepository
    private var cameraId: Long = -1L
    private var cameraName: String = ""
    private var currentSpeed: Int = 4
    private var currentZoomLevel: Int = 50
    private var activeDirection: String? = null
    private lateinit var statusText: TextView
    private val repeatHandler = Handler(Looper.getMainLooper())
    private var repeatCommand: String? = null
    private var ptzPollActive: Boolean = false
    private val ptzPoller = Runnable { pollPtzStateTick() }
    private val repeatRunnable = object : Runnable {
        override fun run() {
            val action = repeatCommand ?: return
            sendCommand(action, silent = true)
            repeatHandler.postDelayed(this, 300L)
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_ptz)

        val appConfig = AppConfig(this)
        repository = CameraRepository(ApiClient.create(appConfig.getBaseUrl(), appConfig::getToken))
        cameraId = intent.getLongExtra(EXTRA_CAMERA_ID, -1L)
        cameraName = intent.getStringExtra(EXTRA_CAMERA_NAME).orEmpty()

        findViewById<android.view.View>(R.id.ptz_back).setOnClickListener { finish() }
        findViewById<android.view.View>(R.id.ptz_more).setOnClickListener { sendCommand("home") }
        findViewById<TextView>(R.id.ptz_title).text =
            if (cameraName.isBlank()) getString(R.string.app_name) else cameraName
        statusText = findViewById(R.id.ptz_status_text)
        statusText.text = if (cameraName.isBlank()) "PTZ Ready" else "PTZ Ready · $cameraName"

        bindDirectionalTouch(R.id.ptz_up, "up")
        bindDirectionalTouch(R.id.ptz_down, "down")
        bindDirectionalTouch(R.id.ptz_left, "left")
        bindDirectionalTouch(R.id.ptz_right, "right")
        findViewById<android.view.View>(R.id.ptz_stop).setOnClickListener {
            activeDirection = null
            stopRepeating()
            sendCommand("stop")
        }

        findViewById<android.view.View>(R.id.ptz_zoom_in).setOnClickListener {
            currentZoomLevel = (currentZoomLevel + 5).coerceAtMost(100)
            sendCommand("zoom_in", zoomStep = 1, zoomLevel = currentZoomLevel)
        }
        findViewById<android.view.View>(R.id.ptz_zoom_out).setOnClickListener {
            currentZoomLevel = (currentZoomLevel - 5).coerceAtLeast(0)
            sendCommand("zoom_out", zoomStep = 1, zoomLevel = currentZoomLevel)
        }

        findViewById<android.view.View>(R.id.ptz_home).setOnClickListener {
            sendCommand("home")
        }
        findViewById<android.view.View>(R.id.ptz_preset_door).setOnClickListener {
            sendCommand("preset", preset = "door")
        }
        findViewById<android.view.View>(R.id.ptz_preset_road).setOnClickListener {
            sendCommand("preset", preset = "road")
        }
        findViewById<android.view.View>(R.id.ptz_preset_garden).setOnClickListener {
            sendCommand("preset", preset = "garden")
        }

        findViewById<SeekBar>(R.id.ptz_speed_seek).setOnSeekBarChangeListener(
            object : SeekBar.OnSeekBarChangeListener {
                override fun onProgressChanged(seekBar: SeekBar?, progress: Int, fromUser: Boolean) {
                    currentSpeed = (progress + 1).coerceIn(1, 10)
                }
                override fun onStartTrackingTouch(seekBar: SeekBar?) = Unit
                override fun onStopTrackingTouch(seekBar: SeekBar?) = Unit
            },
        )
    }

    @SuppressLint("ClickableViewAccessibility")
    private fun bindDirectionalTouch(viewId: Int, action: String) {
        findViewById<android.view.View>(viewId).setOnTouchListener { _, event ->
            when (event.actionMasked) {
                MotionEvent.ACTION_DOWN -> {
                    if (activeDirection != action) {
                        activeDirection = action
                        sendCommand(action)
                        startRepeating(action)
                    }
                    true
                }
                MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> {
                    if (activeDirection == action) {
                        activeDirection = null
                        stopRepeating()
                        sendCommand("stop")
                    }
                    true
                }
                else -> false
            }
        }
    }

    override fun onStop() {
        super.onStop()
        stopRepeating()
        stopPtzPolling()
    }

    override fun onStart() {
        super.onStart()
        startPtzPolling()
    }

    private fun startRepeating(action: String) {
        repeatCommand = action
        repeatHandler.removeCallbacks(repeatRunnable)
        repeatHandler.postDelayed(repeatRunnable, 320L)
    }

    private fun stopRepeating() {
        repeatCommand = null
        repeatHandler.removeCallbacks(repeatRunnable)
    }

    private fun sendCommand(
        action: String,
        zoomStep: Int? = null,
        zoomLevel: Int? = null,
        preset: String? = null,
        silent: Boolean = false,
    ) {
        if (cameraId <= 0L) {
            Toast.makeText(this, "Invalid camera", Toast.LENGTH_SHORT).show()
            return
        }
        lifecycleScope.launch {
            val result = runCatching {
                withContext(Dispatchers.IO) {
                    repository.sendPtzCommand(
                        cameraId = cameraId,
                        action = action,
                        speed = currentSpeed,
                        zoomStep = zoomStep,
                        zoomLevel = zoomLevel,
                        preset = preset,
                    )
                }
            }
            if (result.isSuccess) {
                val rsp = result.getOrNull()
                val applied = rsp?.published != false
                statusText.text = buildString {
                    append("PTZ ")
                    append(action)
                    append(" · speed ")
                    append(currentSpeed)
                    if (!preset.isNullOrBlank()) {
                        append(" · preset ")
                        append(preset)
                    }
                    append(if (applied) " · ok" else " · pending")
                }
                if (!silent) {
                    Toast.makeText(
                        this@PtzActivity,
                        if (applied) "PTZ $action" else "PTZ pending",
                        Toast.LENGTH_SHORT,
                    ).show()
                }
                return@launch
            }
            if (result.isFailure && !silent) {
                val msg = result.exceptionOrNull()?.message ?: "PTZ command failed"
                statusText.text = "PTZ error · $msg"
                Toast.makeText(this@PtzActivity, msg, Toast.LENGTH_SHORT).show()
            }
        }
    }

    private fun startPtzPolling() {
        if (ptzPollActive || cameraId <= 0L) return
        ptzPollActive = true
        statusText.post(ptzPoller)
    }

    private fun stopPtzPolling() {
        if (!ptzPollActive) return
        ptzPollActive = false
        statusText.removeCallbacks(ptzPoller)
    }

    private fun pollPtzStateTick() {
        if (!ptzPollActive || cameraId <= 0L) return
        lifecycleScope.launch {
            try {
                val info = withContext(Dispatchers.IO) { repository.getStreamInfo(cameraId) }
                val seiPtz = info.sei?.ptz
                val action = seiPtz?.action ?: info.device?.ptzAction
                val speed = seiPtz?.speed ?: info.device?.ptzSpeed
                val zoom = seiPtz?.zoomLevel ?: info.device?.ptzZoomLevel
                val pan = seiPtz?.panDeg
                val tilt = seiPtz?.tiltDeg
                val source = if (seiPtz != null) "SEI" else "DEVICE"
                if (!action.isNullOrBlank()) {
                    statusText.text = buildString {
                        append("PTZ ")
                        append(action)
                        if (speed != null) {
                            append(" · speed ")
                            append(speed)
                        }
                        if (zoom != null) {
                            append(" · zoom ")
                            append(zoom)
                        }
                        if (pan != null || tilt != null) {
                            append(" · pan ")
                            append(String.format("%.0f", pan ?: 0.0))
                            append(" · tilt ")
                            append(String.format("%.0f", tilt ?: 0.0))
                        }
                        append(" · ")
                        append(source)
                    }
                }
            } catch (_: Exception) {
            } finally {
                if (ptzPollActive) {
                    statusText.postDelayed(ptzPoller, 2000L)
                }
            }
        }
    }
}
