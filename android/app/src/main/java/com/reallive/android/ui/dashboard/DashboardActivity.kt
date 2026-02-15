package com.reallive.android.ui.dashboard

import android.content.Intent
import android.os.Bundle
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import androidx.lifecycle.lifecycleScope
import androidx.recyclerview.widget.GridLayoutManager
import androidx.recyclerview.widget.RecyclerView
import com.reallive.android.R
import com.reallive.android.config.AppConfig
import com.reallive.android.data.CameraRepository
import com.reallive.android.network.ApiClient
import com.reallive.android.ui.watch.WatchActivity
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

class DashboardActivity : AppCompatActivity() {
    private lateinit var repository: CameraRepository
    private lateinit var adapter: CameraAdapter
    private lateinit var statusText: TextView
    private var refreshJob: Job? = null

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_dashboard)

        statusText = findViewById(R.id.dashboard_status)
        val recyclerView = findViewById<RecyclerView>(R.id.camera_recycler)

        val appConfig = AppConfig(this)
        val api = ApiClient.create(appConfig.getBaseUrl()) { appConfig.getToken() }
        repository = CameraRepository(api)

        adapter = CameraAdapter(appConfig.getBaseUrl()) { camera ->
            val intent = Intent(this, WatchActivity::class.java)
            intent.putExtra(WatchActivity.EXTRA_CAMERA_ID, camera.id)
            intent.putExtra(WatchActivity.EXTRA_CAMERA_NAME, camera.name)
            intent.putExtra(WatchActivity.EXTRA_STREAM_KEY, camera.stream_key)
            intent.putExtra(WatchActivity.EXTRA_BASE_URL, appConfig.getBaseUrl())
            intent.putExtra(WatchActivity.EXTRA_TOKEN, appConfig.getToken())
            startActivity(intent)
        }

        recyclerView.layoutManager = GridLayoutManager(this, 2)
        recyclerView.adapter = adapter
    }

    override fun onStart() {
        super.onStart()
        refreshJob = lifecycleScope.launch(Dispatchers.IO) {
            while (isActive) {
                runCatching {
                    val cameras = repository.listCameras()
                    withContext(Dispatchers.Main) {
                        val streaming = cameras.count { (it.status ?: "").equals("streaming", ignoreCase = true) }
                        val online = cameras.count { (it.status ?: "").equals("online", ignoreCase = true) }
                        statusText.text = "Cameras: ${cameras.size} | Streaming: $streaming | Online: $online"
                        adapter.submitList(cameras)
                    }
                }.onFailure { err ->
                    withContext(Dispatchers.Main) {
                        statusText.text = "Load failed: ${err.message ?: "unknown"}"
                    }
                }
                delay(3000)
            }
        }
    }

    override fun onStop() {
        refreshJob?.cancel()
        refreshJob = null
        super.onStop()
    }
}
