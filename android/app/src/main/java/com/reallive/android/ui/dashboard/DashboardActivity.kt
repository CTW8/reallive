package com.reallive.android.ui.dashboard

import android.content.res.ColorStateList
import android.content.Intent
import android.os.Bundle
import android.text.TextUtils
import android.view.View
import android.view.animation.AnimationUtils
import android.widget.ImageView
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import androidx.lifecycle.lifecycleScope
import coil.load
import com.google.android.material.bottomnavigation.BottomNavigationView
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import com.reallive.android.R
import com.reallive.android.config.AppConfig
import com.reallive.android.data.CameraRepository
import com.reallive.android.network.ApiFactory
import com.reallive.android.network.CameraDto
import com.reallive.android.ui.auth.LoginActivity
import com.reallive.android.ui.auth.AuthGuard
import com.reallive.android.ui.camera.AddCameraActivity
import com.reallive.android.ui.camera.CameraListActivity
import com.reallive.android.ui.camera.CameraSettingsActivity
import com.reallive.android.ui.common.MainTabNavigation
import com.reallive.android.ui.notifications.NotificationsActivity
import com.reallive.android.ui.search.SearchActivity
import com.reallive.android.ui.watch.WatchActivity
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import retrofit2.HttpException
import java.util.Locale

class DashboardActivity : AppCompatActivity() {
    private lateinit var appConfig: AppConfig
    private lateinit var repository: CameraRepository
    private lateinit var subtitleText: TextView
    private lateinit var statTotalText: TextView
    private lateinit var statOnlineText: TextView
    private lateinit var statStreamingText: TextView
    private lateinit var greetingText: TextView
    private lateinit var statTotalLabelText: TextView
    private lateinit var statOnlineLabelText: TextView
    private lateinit var statStreamingLabelText: TextView
    private lateinit var allCamerasTitleText: TextView
    private lateinit var manageText: TextView
    private lateinit var avatarView: TextView
    private lateinit var notificationsDot: View

    private val dashboardItems = mutableListOf<DashboardItemView>()
    private var cameras: List<CameraDto> = emptyList()

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        appConfig = AppConfig(this)
        repository = CameraRepository(ApiFactory.createAuthorized(appConfig))
        if (appConfig.getToken().isNullOrBlank()) {
            redirectToLogin()
            return
        }

        setContentView(R.layout.activity_dashboard)
        bindViews()
        bindActions()
        bindDashboardItems()
        MainTabNavigation.setup(
            activity = this,
            bottomNav = findViewById<BottomNavigationView>(R.id.bottom_nav),
            currentTab = MainTabNavigation.TAB_HOME,
        )
    }

    override fun onStart() {
        super.onStart()
        if (appConfig.shouldRequireReauth()) {
            redirectToLogin(forceReauth = true)
            return
        }
        appConfig.markAuthenticated()
        loadDashboard()
    }

    override fun onStop() {
        super.onStop()
        appConfig.markAppBackgrounded()
    }

    private fun bindViews() {
        avatarView = findViewById(R.id.dashboard_avatar)
        notificationsDot = findViewById(R.id.dashboard_notifications_dot)
        subtitleText = findViewById(R.id.dashboard_subtitle)
        greetingText = findViewById(R.id.dashboard_greeting)
        statTotalText = findViewById(R.id.stat_total)
        statOnlineText = findViewById(R.id.stat_online)
        statStreamingText = findViewById(R.id.stat_streaming)
        statTotalLabelText = findViewById(R.id.stat_total_label)
        statOnlineLabelText = findViewById(R.id.stat_online_label)
        statStreamingLabelText = findViewById(R.id.stat_streaming_label)
        allCamerasTitleText = findViewById(R.id.dashboard_all_cameras_title)
        manageText = findViewById(R.id.btn_open_camera_list)
        applyLocalizedTexts(appConfig.getAppLanguage())
    }

    private fun bindActions() {
        findViewById<View>(R.id.fab_add_camera).setOnClickListener {
            startActivity(Intent(this, AddCameraActivity::class.java))
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
        avatarView.setOnClickListener {
            startActivity(Intent(this, com.reallive.android.ui.settings.ProfileActivity::class.java))
        }

    }

    private fun bindDashboardItems() {
        dashboardItems.clear()
        dashboardItems.add(
            DashboardItemView(
                root = findViewById(R.id.dashboard_item_front),
                title = findViewById(R.id.dashboard_item_front_title),
                subtitle = findViewById(R.id.dashboard_item_front_subtitle),
                statusDot = findViewById(R.id.dashboard_item_front_dot),
                statusText = findViewById(R.id.dashboard_item_front_status),
                thumbnail = findViewById(R.id.dashboard_item_front_thumb),
                fallbackIconRes = R.drawable.ic_rl_door_front_24,
            )
        )
        dashboardItems.add(
            DashboardItemView(
                root = findViewById(R.id.dashboard_item_garage),
                title = findViewById(R.id.dashboard_item_garage_title),
                subtitle = findViewById(R.id.dashboard_item_garage_subtitle),
                statusDot = findViewById(R.id.dashboard_item_garage_dot),
                statusText = findViewById(R.id.dashboard_item_garage_status),
                thumbnail = findViewById(R.id.dashboard_item_garage_thumb),
                fallbackIconRes = R.drawable.ic_rl_garage_24,
            )
        )
        dashboardItems.add(
            DashboardItemView(
                root = findViewById(R.id.dashboard_item_living),
                title = findViewById(R.id.dashboard_item_living_title),
                subtitle = findViewById(R.id.dashboard_item_living_subtitle),
                statusDot = findViewById(R.id.dashboard_item_living_dot),
                statusText = findViewById(R.id.dashboard_item_living_status),
                thumbnail = findViewById(R.id.dashboard_item_living_thumb),
                fallbackIconRes = R.drawable.ic_rl_living_24,
            )
        )
        dashboardItems.add(
            DashboardItemView(
                root = findViewById(R.id.dashboard_item_backyard),
                title = findViewById(R.id.dashboard_item_backyard_title),
                subtitle = findViewById(R.id.dashboard_item_backyard_subtitle),
                statusDot = findViewById(R.id.dashboard_item_backyard_dot),
                statusText = findViewById(R.id.dashboard_item_backyard_status),
                thumbnail = findViewById(R.id.dashboard_item_backyard_thumb),
                fallbackIconRes = R.drawable.ic_rl_deck_24,
            )
        )
        dashboardItems.add(
            DashboardItemView(
                root = findViewById(R.id.dashboard_item_warehouse),
                title = findViewById(R.id.dashboard_item_warehouse_title),
                subtitle = findViewById(R.id.dashboard_item_warehouse_subtitle),
                statusDot = findViewById(R.id.dashboard_item_warehouse_dot),
                statusText = findViewById(R.id.dashboard_item_warehouse_status),
                thumbnail = findViewById(R.id.dashboard_item_warehouse_thumb),
                fallbackIconRes = R.drawable.ic_rl_warehouse_24,
            )
        )
        dashboardItems.add(
            DashboardItemView(
                root = findViewById(R.id.dashboard_item_bedroom),
                title = findViewById(R.id.dashboard_item_bedroom_title),
                subtitle = findViewById(R.id.dashboard_item_bedroom_subtitle),
                statusDot = findViewById(R.id.dashboard_item_bedroom_dot),
                statusText = findViewById(R.id.dashboard_item_bedroom_status),
                thumbnail = findViewById(R.id.dashboard_item_bedroom_thumb),
                fallbackIconRes = R.drawable.ic_rl_bedroom_parent_24,
            )
        )
        dashboardItems.add(
            DashboardItemView(
                root = findViewById(R.id.dashboard_item_office),
                title = findViewById(R.id.dashboard_item_office_title),
                subtitle = findViewById(R.id.dashboard_item_office_subtitle),
                statusDot = findViewById(R.id.dashboard_item_office_dot),
                statusText = findViewById(R.id.dashboard_item_office_status),
                thumbnail = findViewById(R.id.dashboard_item_office_thumb),
                fallbackIconRes = R.drawable.ic_rl_meeting_room_24,
            )
        )
        dashboardItems.add(
            DashboardItemView(
                root = findViewById(R.id.dashboard_item_parking),
                title = findViewById(R.id.dashboard_item_parking_title),
                subtitle = findViewById(R.id.dashboard_item_parking_subtitle),
                statusDot = findViewById(R.id.dashboard_item_parking_dot),
                statusText = findViewById(R.id.dashboard_item_parking_status),
                thumbnail = findViewById(R.id.dashboard_item_parking_thumb),
                fallbackIconRes = R.drawable.ic_rl_local_parking_24,
            )
        )
        dashboardItems.forEach { item ->
            item.title.maxLines = 1
            item.title.ellipsize = TextUtils.TruncateAt.END
            item.subtitle.maxLines = 2
            item.subtitle.ellipsize = TextUtils.TruncateAt.END
        }
    }

    private fun loadDashboard() {
        val zh = isChineseLanguage(appConfig.getAppLanguage())
        subtitleText.text = if (zh) "正在加载账户..." else "Loading account..."
        avatarView.text = "U"
        statTotalText.text = "--"
        statOnlineText.text = "--"
        statStreamingText.text = "--"
        notificationsDot.visibility = View.GONE
        dashboardItems.forEach { it.root.visibility = View.GONE }
        lifecycleScope.launch {
            try {
                val settings = withContext(Dispatchers.IO) { repository.getSettings() }
                appConfig.setUsername(settings.profile.displayName)
                appConfig.setEmail(settings.profile.email)
                appConfig.setAutoLockSec(settings.system.autoLockSec)
                val displayName = settings.profile.displayName.ifBlank {
                    appConfig.getUsername().orEmpty().ifBlank { if (zh) "用户" else "User" }
                }
                greetingText.text = if (zh) "你好，$displayName" else "Hi, $displayName"
                subtitleText.text = if (zh) "欢迎回来，$displayName" else "Welcome back, $displayName"
                avatarView.text = displayName.trim().firstOrNull()?.uppercase() ?: "U"

                val stats = withContext(Dispatchers.IO) { repository.getDashboardStats() }
                statTotalText.text = stats.cameras.online.toString()
                statOnlineText.text = stats.cameras.offline.toString()

                cameras = withContext(Dispatchers.IO) { repository.listCameras() }
                val unread = withContext(Dispatchers.IO) { repository.getUnreadCount() }
                statStreamingText.text = unread.count.toString()
                notificationsDot.visibility = if (unread.count > 0) View.VISIBLE else View.GONE
                renderCameras()
            } catch (ex: Exception) {
                if (ex is HttpException && ex.code() == 401) {
                    val valid = withContext(Dispatchers.IO) { AuthGuard.isSessionValid(appConfig) }
                    if (valid) {
                        loadDashboard()
                    } else {
                        appConfig.clearAuth()
                        redirectToLogin()
                    }
                } else {
                    subtitleText.text = if (zh) "同步仪表盘失败" else "Unable to sync dashboard"
                }
            }
        }
    }

    private fun renderCameras() {
        dashboardItems.forEachIndexed { index, item ->
            val camera = cameras.getOrNull(index)
            if (camera == null) {
                item.root.visibility = View.GONE
                return@forEachIndexed
            }
            item.root.visibility = View.VISIBLE
            item.root.setBackgroundResource(R.drawable.bg_dashboard_camera_item)
            item.root.setPadding(dp(10), dp(10), dp(10), dp(10))
            item.title.text = camera.name
            val status = (camera.status ?: "offline").lowercase(Locale.US)
            val statusLabel = when (status) {
                "streaming" -> "REC"
                "online" -> if (isChineseLanguage(appConfig.getAppLanguage())) "在线" else "Online"
                else -> if (isChineseLanguage(appConfig.getAppLanguage())) "离线" else "Offline"
            }
            val meta = camera.resolution ?: "Auto"
            val subtitleStatus = if (status == "streaming") {
                if (isChineseLanguage(appConfig.getAppLanguage())) "录制中" else "Recording"
            } else {
                statusLabel
            }
            item.subtitle.text = "$meta · $subtitleStatus"
            item.statusText.text = statusLabel
            when (status) {
                "streaming" -> {
                    item.statusDot.setBackgroundResource(R.drawable.bg_status_dot_orange)
                    item.statusText.setTextColor(getColor(R.color.rl_warning))
                    item.statusDot.startAnimation(AnimationUtils.loadAnimation(this, R.anim.rl_blink))
                }
                "online" -> {
                    item.statusDot.setBackgroundResource(R.drawable.bg_status_dot_green)
                    item.statusText.setTextColor(getColor(R.color.rl_success))
                    item.statusDot.clearAnimation()
                }
                else -> {
                    item.statusDot.setBackgroundResource(R.drawable.bg_status_dot_red)
                    item.statusText.setTextColor(getColor(R.color.rl_error))
                    item.statusDot.clearAnimation()
                }
            }
            val thumbnailUrl = camera.thumbnailUrl?.takeIf { it.isNotBlank() }?.let { resolveMediaUrl(it) }
            if (thumbnailUrl != null) {
                item.thumbnail.scaleType = ImageView.ScaleType.CENTER_CROP
                item.thumbnail.setPadding(0, 0, 0, 0)
                item.thumbnail.imageTintList = null
                item.thumbnail.load(thumbnailUrl)
            } else {
                item.thumbnail.load(null)
                item.thumbnail.setImageResource(item.fallbackIconRes)
                item.thumbnail.scaleType = ImageView.ScaleType.CENTER_INSIDE
                val pad = dp(11)
                item.thumbnail.setPadding(pad, pad, pad, pad)
                item.thumbnail.imageTintList = ColorStateList.valueOf(getColor(R.color.rl_text_secondary))
            }
            item.root.setOnClickListener { openWatch(camera) }
        }
    }

    private fun dp(value: Int): Int = (value * resources.displayMetrics.density).toInt()

    private fun resolveMediaUrl(raw: String): String {
        if (raw.startsWith("http://") || raw.startsWith("https://")) return raw
        val base = appConfig.getBaseUrl().removeSuffix("/")
        val path = if (raw.startsWith("/")) raw else "/$raw"
        return base + path
    }

    private fun redirectToLogin(forceReauth: Boolean = false) {
        startActivity(
            Intent(this, LoginActivity::class.java).apply {
                if (forceReauth) {
                    putExtra(LoginActivity.EXTRA_FORCE_REAUTH, true)
                }
            },
        )
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
        val zh = isChineseLanguage(appConfig.getAppLanguage())
        val options = arrayOf(
            if (zh) "打开直播" else "Open Live",
            if (zh) "摄像头设置" else "Camera Settings",
        )
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

    private fun applyLocalizedTexts(languageCode: String) {
        val zh = isChineseLanguage(languageCode)
        greetingText.text = if (zh) "你好，用户" else "Hi, User"
        subtitleText.text = if (zh) "欢迎回来" else "Welcome back"
        statTotalLabelText.text = if (zh) "在线" else "Online"
        statOnlineLabelText.text = if (zh) "离线" else "Offline"
        statStreamingLabelText.text = if (zh) "告警" else "Alerts"
        allCamerasTitleText.text = if (zh) "全部摄像头" else "All Cameras"
        manageText.text = if (zh) "管理" else "Manage"
    }

    private fun isChineseLanguage(languageCode: String?): Boolean {
        if (languageCode.isNullOrBlank()) return false
        return languageCode.lowercase(Locale.US).startsWith("zh")
    }

    private data class DashboardItemView(
        val root: View,
        val title: TextView,
        val subtitle: TextView,
        val statusDot: View,
        val statusText: TextView,
        val thumbnail: ImageView,
        val fallbackIconRes: Int,
    )
}
