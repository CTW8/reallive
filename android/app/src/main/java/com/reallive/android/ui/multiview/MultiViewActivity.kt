package com.reallive.android.ui.multiview

import android.content.Intent
import android.os.Bundle
import android.view.View
import androidx.appcompat.app.AppCompatActivity
import com.google.android.material.appbar.MaterialToolbar
import com.google.android.material.bottomnavigation.BottomNavigationView
import com.reallive.android.R
import com.reallive.android.config.AppConfig
import com.reallive.android.ui.auth.LoginActivity
import com.reallive.android.ui.common.MainTabNavigation
import com.reallive.android.ui.watch.WatchActivity

class MultiViewActivity : AppCompatActivity() {
    private lateinit var appConfig: AppConfig

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        appConfig = AppConfig(this)
        if (appConfig.getToken().isNullOrBlank()) {
            startActivity(Intent(this, LoginActivity::class.java))
            finish()
            return
        }
        setContentView(R.layout.activity_multiview)

        findViewById<MaterialToolbar>(R.id.multiview_toolbar).setNavigationOnClickListener { finish() }
        bindCell(R.id.multiview_cell_front, 1L, "Front Door", "front-door")
        bindCell(R.id.multiview_cell_garage, 2L, "Garage", "garage")
        bindCell(R.id.multiview_cell_living, 3L, "Living Room", "living-room")
        bindCell(R.id.multiview_cell_backyard, 4L, "Backyard", "backyard")

        MainTabNavigation.setup(
            activity = this,
            bottomNav = findViewById<BottomNavigationView>(R.id.bottom_nav),
            currentTab = MainTabNavigation.TAB_MONITOR,
        )
    }

    private fun bindCell(viewId: Int, cameraId: Long, cameraName: String, streamKey: String) {
        findViewById<View>(viewId).setOnClickListener {
            startActivity(
                Intent(this, WatchActivity::class.java).apply {
                    putExtra(WatchActivity.EXTRA_CAMERA_ID, cameraId)
                    putExtra(WatchActivity.EXTRA_CAMERA_NAME, cameraName)
                    putExtra(WatchActivity.EXTRA_STREAM_KEY, streamKey)
                },
            )
        }
    }
}
