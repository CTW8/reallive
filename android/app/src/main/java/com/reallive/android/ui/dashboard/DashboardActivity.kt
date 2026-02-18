package com.reallive.android.ui.dashboard

import android.content.Intent
import android.os.Bundle
import android.view.View
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import androidx.recyclerview.widget.LinearLayoutManager
import androidx.recyclerview.widget.RecyclerView
import com.google.android.material.bottomnavigation.BottomNavigationView
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import com.reallive.android.R
import com.reallive.android.config.AppConfig
import com.reallive.android.network.CameraDto
import com.reallive.android.ui.auth.LoginActivity
import com.reallive.android.ui.camera.AddCameraActivity
import com.reallive.android.ui.camera.CameraListActivity
import com.reallive.android.ui.camera.CameraSettingsActivity
import com.reallive.android.ui.common.MainTabNavigation
import com.reallive.android.ui.notifications.NotificationsActivity
import com.reallive.android.ui.search.SearchActivity
import com.reallive.android.ui.watch.WatchActivity

class DashboardActivity : AppCompatActivity() {
    private lateinit var appConfig: AppConfig
    private lateinit var cameraAdapter: CameraAdapter
    private lateinit var subtitleText: TextView
    private lateinit var statTotalText: TextView
    private lateinit var statOnlineText: TextView
    private lateinit var statStreamingText: TextView
    private lateinit var emptyHintText: TextView

    private val cameras = listOf(
        CameraDto(1, "Front Door", "front-door", resolution = "1080p", status = "streaming"),
        CameraDto(2, "Garage", "garage", resolution = "1080p", status = "online"),
        CameraDto(3, "Living Room", "living-room", resolution = "2K", status = "online"),
        CameraDto(4, "Backyard", "backyard", resolution = "1080p", status = "offline"),
        CameraDto(5, "Warehouse", "warehouse", resolution = "4K", status = "online"),
        CameraDto(6, "Bedroom", "bedroom", resolution = "1080p", status = "online"),
    )

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        appConfig = AppConfig(this)
        if (appConfig.getToken().isNullOrBlank()) {
            redirectToLogin()
            return
        }

        setContentView(R.layout.activity_dashboard)
        bindViews()
        bindActions()
        renderStaticData()
        MainTabNavigation.setup(
            activity = this,
            bottomNav = findViewById<BottomNavigationView>(R.id.bottom_nav),
            currentTab = MainTabNavigation.TAB_HOME,
        )
    }

    private fun bindViews() {
        subtitleText = findViewById(R.id.dashboard_subtitle)
        statTotalText = findViewById(R.id.stat_total)
        statOnlineText = findViewById(R.id.stat_online)
        statStreamingText = findViewById(R.id.stat_streaming)
        emptyHintText = findViewById(R.id.empty_hint)

        val cameraRecycler = findViewById<RecyclerView>(R.id.camera_recycler)
        cameraAdapter = CameraAdapter(
            baseUrl = "",
            onClick = { openWatch(it) },
            onLongClick = { showCameraActions(it) },
            onMenuClick = { showCameraActions(it) },
        )
        cameraRecycler.layoutManager = LinearLayoutManager(this)
        cameraRecycler.adapter = cameraAdapter
    }

    private fun bindActions() {
        findViewById<View>(R.id.fab_add_camera).setOnClickListener {
            startActivity(Intent(this, AddCameraActivity::class.java))
        }
        findViewById<View>(R.id.btn_filter_cameras).setOnClickListener {
            startActivity(Intent(this, CameraListActivity::class.java))
        }
        findViewById<View>(R.id.btn_open_camera_list).setOnClickListener {
            startActivity(Intent(this, CameraListActivity::class.java))
        }
        findViewById<View>(R.id.btn_open_search).setOnClickListener {
            startActivity(Intent(this, SearchActivity::class.java))
        }
        findViewById<View>(R.id.btn_open_notifications).setOnClickListener {
            startActivity(Intent(this, NotificationsActivity::class.java))
        }
        findViewById<View>(R.id.btn_preview_front).setOnClickListener {
            openWatch(cameras.first())
        }
        findViewById<View>(R.id.btn_preview_garage).setOnClickListener {
            openWatch(cameras[1])
        }
        findViewById<View>(R.id.btn_preview_backyard).setOnClickListener {
            openWatch(cameras[3])
        }
    }

    private fun renderStaticData() {
        subtitleText.text = "Welcome back"
        statTotalText.text = "6"
        statOnlineText.text = "2"
        statStreamingText.text = "3"

        cameraAdapter.submitList(cameras)
        emptyHintText.visibility = if (cameras.isEmpty()) View.VISIBLE else View.GONE
    }

    private fun redirectToLogin() {
        startActivity(Intent(this, LoginActivity::class.java))
        finish()
    }

    private fun openWatch(camera: CameraDto) {
        startActivity(
            Intent(this, WatchActivity::class.java).apply {
                putExtra(WatchActivity.EXTRA_CAMERA_ID, camera.id)
                putExtra(WatchActivity.EXTRA_CAMERA_NAME, camera.name)
                putExtra(WatchActivity.EXTRA_STREAM_KEY, camera.stream_key)
            },
        )
    }

    private fun showCameraActions(camera: CameraDto) {
        val options = arrayOf("Open Live", "Camera Settings")
        MaterialAlertDialogBuilder(this)
            .setTitle(camera.name)
            .setItems(options) { _, which ->
                when (which) {
                    0 -> openWatch(camera)
                    1 -> startActivity(
                        Intent(this, CameraSettingsActivity::class.java).apply {
                            putExtra(CameraSettingsActivity.EXTRA_CAMERA_ID, camera.id)
                            putExtra(CameraSettingsActivity.EXTRA_CAMERA_NAME, camera.name)
                        },
                    )
                }
            }
            .show()
    }
}
