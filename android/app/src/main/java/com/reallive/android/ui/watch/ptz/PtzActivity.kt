package com.reallive.android.ui.watch.ptz

import android.annotation.SuppressLint
import android.content.Intent
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.view.HapticFeedbackConstants
import android.view.MotionEvent
import android.view.View
import android.widget.ImageView
import android.widget.SeekBar
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import androidx.lifecycle.lifecycleScope
import com.reallive.android.R
import com.reallive.android.config.AppConfig
import com.reallive.android.data.CameraRepository
import com.reallive.android.network.ApiFactory
import com.reallive.android.ui.auth.AuthGuard
import com.reallive.android.ui.auth.LoginActivity
import com.reallive.android.watch.WatchSessionManager
import com.reallive.player.Player
import com.reallive.player.PlayerFactory
import com.reallive.player.PlayerSurfaceView
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import retrofit2.HttpException
import kotlin.math.atan2
import kotlin.math.hypot
import kotlin.math.roundToInt

class PtzActivity : AppCompatActivity() {
    companion object {
        const val EXTRA_CAMERA_ID = "extra_camera_id"
        const val EXTRA_CAMERA_NAME = "extra_camera_name"
    }

    private lateinit var appConfig: AppConfig
    private lateinit var repository: CameraRepository
    private var cameraId: Long = -1L
    private var cameraName: String = ""
    private var currentSpeed: Int = 4
    private var currentZoomLevel: Int = 50
    private var activeDirection: String? = null
    private lateinit var statusText: TextView
    private lateinit var speedValueText: TextView
    private lateinit var zoomValueText: TextView
    private lateinit var speedSeekBar: SeekBar
    private lateinit var zoomSeekBar: SeekBar
    private lateinit var videoWrap: View
    private lateinit var playerView: PlayerSurfaceView
    private lateinit var placeholderIcon: View
    private lateinit var stopButton: View
    private lateinit var stopDot: View
    private lateinit var padTouchArea: View
    private lateinit var upControl: View
    private lateinit var downControl: View
    private lateinit var leftControl: View
    private lateinit var rightControl: View
    private lateinit var edgeUp: PtzArcHighlightView
    private lateinit var edgeDown: PtzArcHighlightView
    private lateinit var edgeLeft: PtzArcHighlightView
    private lateinit var edgeRight: PtzArcHighlightView
    private lateinit var arrowUp: ImageView
    private lateinit var arrowDown: ImageView
    private lateinit var arrowLeft: ImageView
    private lateinit var arrowRight: ImageView
    private var player: Player? = null
    private var streamFlv: String? = null
    private var streamHls: String? = null
    private var currentPlayUrl: String? = null
    private var watchSession: WatchSessionManager? = null
    private var authRecoverInProgress: Boolean = false
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
        appConfig = AppConfig(this)
        if (appConfig.getToken().isNullOrBlank()) {
            redirectToLogin()
            return
        }
        setContentView(R.layout.activity_ptz)

        repository = CameraRepository(ApiFactory.createAuthorized(appConfig))
        cameraId = intent.getLongExtra(EXTRA_CAMERA_ID, -1L)
        cameraName = intent.getStringExtra(EXTRA_CAMERA_NAME).orEmpty()
        videoWrap = findViewById(R.id.ptz_video_wrap)
        playerView = findViewById(R.id.ptz_player_view)
        placeholderIcon = findViewById(R.id.ptz_placeholder_icon)
        stopButton = findViewById(R.id.ptz_stop)
        stopDot = findViewById(R.id.ptz_stop_dot)
        padTouchArea = findViewById(R.id.ptz_pad_touch_area)
        upControl = findViewById(R.id.ptz_up)
        downControl = findViewById(R.id.ptz_down)
        leftControl = findViewById(R.id.ptz_left)
        rightControl = findViewById(R.id.ptz_right)
        edgeUp = findViewById(R.id.ptz_edge_up)
        edgeDown = findViewById(R.id.ptz_edge_down)
        edgeLeft = findViewById(R.id.ptz_edge_left)
        edgeRight = findViewById(R.id.ptz_edge_right)
        edgeUp.setArc(225f, 90f)
        edgeRight.setArc(-45f, 90f)
        edgeDown.setArc(45f, 90f)
        edgeLeft.setArc(135f, 90f)
        arrowUp = findViewById(R.id.ptz_arrow_up)
        arrowDown = findViewById(R.id.ptz_arrow_down)
        arrowLeft = findViewById(R.id.ptz_arrow_left)
        arrowRight = findViewById(R.id.ptz_arrow_right)

        findViewById<View>(R.id.ptz_back).setOnClickListener { navigateBackToWatch() }
        findViewById<View>(R.id.ptz_more).setOnClickListener { sendCommand("home") }
        findViewById<TextView>(R.id.ptz_title).text =
            if (cameraName.isBlank()) getString(R.string.app_name) else cameraName
        statusText = findViewById(R.id.ptz_status_text)
        speedValueText = findViewById(R.id.ptz_speed_value)
        zoomValueText = findViewById(R.id.ptz_zoom_value)
        speedSeekBar = findViewById(R.id.ptz_speed_seek)
        zoomSeekBar = findViewById(R.id.ptz_zoom_seek)
        statusText.text = if (cameraName.isBlank()) "PTZ Ready" else "PTZ Ready · $cameraName"

        bindPadTouch()
        stopButton.setOnClickListener {
            activeDirection = null
            stopRepeating()
            animateStopFeedback()
            resetPadVisualState()
            sendCommand("stop")
        }

        findViewById<View>(R.id.ptz_zoom_in).setOnClickListener {
            setZoomLevel((currentZoomLevel + 5).coerceAtMost(100))
            sendCommand("zoom_in", zoomStep = 1, zoomLevel = currentZoomLevel)
        }
        findViewById<View>(R.id.ptz_zoom_out).setOnClickListener {
            setZoomLevel((currentZoomLevel - 5).coerceAtLeast(0))
            sendCommand("zoom_out", zoomStep = 1, zoomLevel = currentZoomLevel)
        }

        speedSeekBar.max = 4
        speedSeekBar.progress = currentSpeed - 1
        speedSeekBar.setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
            override fun onProgressChanged(seekBar: SeekBar?, progress: Int, fromUser: Boolean) {
                currentSpeed = (progress + 1).coerceIn(1, 5)
                renderSpeedState()
            }

            override fun onStartTrackingTouch(seekBar: SeekBar?) = Unit

            override fun onStopTrackingTouch(seekBar: SeekBar?) {
                statusText.text = "PTZ speed set · $currentSpeed"
            }
        })

        zoomSeekBar.max = 100
        zoomSeekBar.progress = currentZoomLevel
        zoomSeekBar.setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
            override fun onProgressChanged(seekBar: SeekBar?, progress: Int, fromUser: Boolean) {
                currentZoomLevel = progress.coerceIn(0, 100)
                renderZoomState()
            }

            override fun onStartTrackingTouch(seekBar: SeekBar?) = Unit

            override fun onStopTrackingTouch(seekBar: SeekBar?) {
                sendCommand("zoom_set", zoomLevel = currentZoomLevel, silent = true)
                statusText.text = "PTZ zoom set · $currentZoomLevel%"
            }
        })
        renderSpeedState()
        renderZoomState()
        resetPadVisualState()

        setupPlayer()
        fitPlayerHeightByAspect()
    }

    override fun onStart() {
        super.onStart()
        if (appConfig.shouldRequireReauth()) {
            redirectToLogin(forceReauth = true)
            return
        }
        appConfig.markAuthenticated()
        loadStream()
        startPtzPolling()
    }

    override fun onStop() {
        super.onStop()
        stopRepeating()
        stopPtzPolling()
        lifecycleScope.launch {
            watchSession?.stop()
            watchSession = null
        }
    }

    override fun onDestroy() {
        super.onDestroy()
        playerView.unbindPlayer()
        player?.stop()
        player?.release()
        player = null
    }

    @SuppressLint("ClickableViewAccessibility")
    private fun bindPadTouch() {
        padTouchArea.setOnTouchListener { view, event ->
            when (event.actionMasked) {
                MotionEvent.ACTION_DOWN -> {
                    val action = directionFromTouch(event.x, event.y)
                    if (action != null && activeDirection != action) {
                        activeDirection = action
                        animateDirectionFeedback(action)
                        view.performHapticFeedback(HapticFeedbackConstants.VIRTUAL_KEY)
                        sendCommand(action)
                        startRepeating(action)
                    }
                    true
                }
                MotionEvent.ACTION_MOVE -> {
                    val action = directionFromTouch(event.x, event.y)
                    if (action != null && activeDirection != action) {
                        activeDirection = action
                        animateDirectionFeedback(action)
                        sendCommand(action, silent = true)
                        startRepeating(action)
                    }
                    true
                }
                MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> {
                    if (activeDirection != null) {
                        activeDirection = null
                        resetPadVisualState()
                        stopRepeating()
                        sendCommand("stop")
                    }
                    true
                }
                else -> false
            }
        }
    }

    private fun directionFromTouch(x: Float, y: Float): String? {
        val cx = padTouchArea.width / 2f
        val cy = padTouchArea.height / 2f
        val dx = x - cx
        val dy = y - cy
        val r = hypot(dx.toDouble(), dy.toDouble()).toFloat()
        val deadZone = (padTouchArea.width * 0.16f).coerceAtLeast(18f)
        if (r < deadZone) return null
        val angle = Math.toDegrees(atan2(dy.toDouble(), dx.toDouble()))
        return when {
            angle >= -45.0 && angle < 45.0 -> "right"
            angle >= 45.0 && angle < 135.0 -> "down"
            angle >= 135.0 || angle < -135.0 -> "left"
            else -> "up"
        }
    }

    private fun setupPlayer() {
        player = PlayerFactory.create()
        player?.let { playerView.bindPlayer(it) }
    }

    private fun fitPlayerHeightByAspect() {
        videoWrap.post {
            val width = videoWrap.width.takeIf { it > 0 } ?: return@post
            val targetHeight = (width * 9f / 16f).roundToInt()
            val lp = videoWrap.layoutParams
            if (lp.height != targetHeight) {
                lp.height = targetHeight
                videoWrap.layoutParams = lp
            }
        }
    }

    private fun loadStream() {
        if (cameraId <= 0L) return
        lifecycleScope.launch {
            try {
                val info = withContext(Dispatchers.IO) { repository.getStreamInfo(cameraId) }
                ensurePlayback(info)
                watchSession = WatchSessionManager(
                    api = ApiFactory.createAuthorized(appConfig),
                    cameraId = cameraId,
                    onUnauthorized = {
                        runOnUiThread { handleUnauthorizedForPtz() }
                    },
                )
                watchSession?.start()
            } catch (ex: Exception) {
                if (ex is HttpException && ex.code() == 401) {
                    handleUnauthorizedForPtz()
                }
            }
        }
    }

    private fun handleUnauthorizedForPtz() {
        if (authRecoverInProgress) return
        authRecoverInProgress = true
        lifecycleScope.launch {
            val valid = withContext(Dispatchers.IO) { AuthGuard.isSessionValid(appConfig) }
            authRecoverInProgress = false
            if (valid) {
                loadStream()
            } else {
                appConfig.clearAuth()
                redirectToLogin()
            }
        }
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

    private fun renderSpeedState() {
        speedValueText.text = currentSpeed.toString()
    }

    private fun setZoomLevel(zoom: Int) {
        currentZoomLevel = zoom.coerceIn(0, 100)
        if (::zoomSeekBar.isInitialized && zoomSeekBar.progress != currentZoomLevel) {
            zoomSeekBar.progress = currentZoomLevel
        }
        renderZoomState()
    }

    private fun renderZoomState() {
        zoomValueText.text = "$currentZoomLevel%"
    }

    private fun animateDirectionFeedback(action: String) {
        val shift = (stopButton.width * 0.16f).takeIf { it > 0f } ?: 8f
        val tx = when (action) {
            "left" -> -shift
            "right" -> shift
            else -> 0f
        }
        val ty = when (action) {
            "up" -> -shift
            "down" -> shift
            else -> 0f
        }
        stopDot.animate().translationX(tx).translationY(ty).setDuration(90L).start()
        stopButton.animate().scaleX(1.03f).scaleY(1.03f).setDuration(90L).start()
        upControl.isPressed = false
        downControl.isPressed = false
        leftControl.isPressed = false
        rightControl.isPressed = false
        edgeUp.animate().alpha(if (action == "up") 1f else 0f).setDuration(90L).start()
        edgeDown.animate().alpha(if (action == "down") 1f else 0f).setDuration(90L).start()
        edgeLeft.animate().alpha(if (action == "left") 1f else 0f).setDuration(90L).start()
        edgeRight.animate().alpha(if (action == "right") 1f else 0f).setDuration(90L).start()
        arrowUp.alpha = if (action == "up") 1f else 0.72f
        arrowDown.alpha = if (action == "down") 1f else 0.72f
        arrowLeft.alpha = if (action == "left") 1f else 0.72f
        arrowRight.alpha = if (action == "right") 1f else 0.72f
    }

    private fun animateStopFeedback() {
        stopButton.performHapticFeedback(HapticFeedbackConstants.KEYBOARD_TAP)
        stopButton.animate().scaleX(0.94f).scaleY(0.94f).setDuration(70L).withEndAction {
            stopButton.animate().scaleX(1f).scaleY(1f).setDuration(120L).start()
        }.start()
        stopDot.animate().translationX(0f).translationY(0f).setDuration(120L).start()
    }

    private fun resetPadVisualState() {
        stopDot.animate().translationX(0f).translationY(0f).setDuration(120L).start()
        stopButton.animate().scaleX(1f).scaleY(1f).setDuration(120L).start()
        upControl.isPressed = false
        downControl.isPressed = false
        leftControl.isPressed = false
        rightControl.isPressed = false
        edgeUp.animate().alpha(0f).setDuration(120L).start()
        edgeDown.animate().alpha(0f).setDuration(120L).start()
        edgeLeft.animate().alpha(0f).setDuration(120L).start()
        edgeRight.animate().alpha(0f).setDuration(120L).start()
        arrowUp.alpha = 0.82f
        arrowDown.alpha = 0.82f
        arrowLeft.alpha = 0.82f
        arrowRight.alpha = 0.82f
    }

    private fun sendCommand(
        action: String,
        zoomStep: Int? = null,
        zoomLevel: Int? = null,
        preset: String? = null,
        silent: Boolean = false,
    ) {
        if (cameraId <= 0L) {
            statusText.text = "PTZ error · invalid camera"
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
                return@launch
            }
            if (result.isFailure && !silent) {
                val msg = result.exceptionOrNull()?.message ?: "PTZ command failed"
                statusText.text = "PTZ error · $msg"
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
                ensurePlayback(info)
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

    private fun ensurePlayback(info: com.reallive.android.network.StreamInfoDto) {
        streamFlv = info.stream_urls?.pull_flv
        streamHls = info.stream_urls?.pull_hls
        val url = streamFlv ?: streamHls
        if (url.isNullOrBlank()) {
            placeholderIcon.visibility = View.VISIBLE
            currentPlayUrl = null
            return
        }
        placeholderIcon.visibility = View.GONE
        val changed = currentPlayUrl.isNullOrBlank() || currentPlayUrl != url
        if (changed) {
            currentPlayUrl = url
            player?.playLive(url)
        }
    }

    private fun navigateBackToWatch() {
        appConfig.markAuthenticated()
        finish()
    }

    private fun redirectToLogin(forceReauth: Boolean = false) {
        startActivity(
            Intent(this, LoginActivity::class.java).apply {
                if (forceReauth) putExtra(LoginActivity.EXTRA_FORCE_REAUTH, true)
            },
        )
        finish()
    }
}
