package com.reallive.android.ui.camera

import android.content.Intent
import android.os.Bundle
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import androidx.lifecycle.lifecycleScope
import com.reallive.android.R
import com.reallive.android.config.AppConfig
import com.reallive.android.data.CameraRepository
import com.reallive.android.network.ApiFactory
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

class CameraConnectProgressActivity : AppCompatActivity() {
    private lateinit var appConfig: AppConfig
    private lateinit var repository: CameraRepository
    private var createJob: Job? = null
    private var creating = false

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        appConfig = AppConfig(this)
        if (appConfig.getToken().isNullOrBlank()) {
            finish()
            return
        }
        repository = CameraRepository(ApiFactory.createAuthorized(appConfig))
        setContentView(R.layout.activity_camera_connect_progress)
        findViewById<android.view.View>(R.id.camera_connect_progress_back).setOnClickListener {
            cancelCreateAndFinish()
        }
        findViewById<android.view.View>(R.id.camera_connect_progress_card).setOnClickListener { }
        findViewById<android.view.View>(R.id.camera_connect_cancel).setOnClickListener {
            cancelCreateAndFinish()
        }
        startRealCreate()
    }

    private fun startRealCreate() {
        if (creating) return
        creating = true
        val name = intent.getStringExtra(EXTRA_CAMERA_NAME).orEmpty().ifBlank { "New Camera" }
        val resolution = intent.getStringExtra(EXTRA_CAMERA_RESOLUTION).orEmpty().ifBlank { "1080p" }
        val source = intent.getStringExtra(EXTRA_SETUP_SOURCE).orEmpty().ifBlank { "manual" }
        val streamKey = intent.getStringExtra(EXTRA_STREAM_KEY).orEmpty().trim().ifBlank { null }
        createJob = lifecycleScope.launch {
            try {
                delay(900)
                val camera = withContext(Dispatchers.IO) { repository.createCamera(name, resolution, streamKey) }
                if (isFinishing || isDestroyed) return@launch
                startActivity(
                    Intent(this@CameraConnectProgressActivity, CameraConnectSuccessActivity::class.java).apply {
                        putExtra(CameraConnectSuccessActivity.EXTRA_CAMERA_ID, camera.id)
                        putExtra(CameraConnectSuccessActivity.EXTRA_CAMERA_NAME, camera.name)
                        putExtra(CameraConnectSuccessActivity.EXTRA_STREAM_KEY, camera.stream_key)
                        putExtra(CameraConnectSuccessActivity.EXTRA_CAMERA_RESOLUTION, camera.resolution ?: resolution)
                        putExtra(CameraConnectSuccessActivity.EXTRA_SETUP_SOURCE, source)
                    },
                )
                finish()
            } catch (_: Exception) {
                if (isFinishing || isDestroyed) return@launch
                Toast.makeText(this@CameraConnectProgressActivity, "Create camera failed", Toast.LENGTH_SHORT).show()
                startActivity(
                    Intent(this@CameraConnectProgressActivity, CameraConnectFailedActivity::class.java).apply {
                        putExtra(EXTRA_CAMERA_NAME, name)
                        putExtra(EXTRA_CAMERA_RESOLUTION, resolution)
                        putExtra(EXTRA_SETUP_SOURCE, source)
                        if (!streamKey.isNullOrBlank()) putExtra(EXTRA_STREAM_KEY, streamKey)
                    },
                )
                finish()
            }
        }
    }

    private fun cancelCreateAndFinish() {
        createJob?.cancel()
        finish()
    }

    override fun onDestroy() {
        createJob?.cancel()
        super.onDestroy()
    }

    companion object {
        const val EXTRA_CAMERA_NAME = "extra_camera_name"
        const val EXTRA_CAMERA_RESOLUTION = "extra_camera_resolution"
        const val EXTRA_SETUP_SOURCE = "extra_setup_source"
        const val EXTRA_STREAM_KEY = "extra_stream_key"
    }
}
