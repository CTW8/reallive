package com.reallive.android.ui.notifications

import android.content.Intent
import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.graphics.drawable.GradientDrawable
import android.text.format.DateUtils
import android.widget.FrameLayout
import android.widget.ImageView
import android.widget.TextView
import android.widget.Toast
import android.widget.EditText
import androidx.appcompat.app.AppCompatActivity
import androidx.core.content.ContextCompat
import androidx.lifecycle.lifecycleScope
import androidx.recyclerview.widget.DiffUtil
import androidx.recyclerview.widget.LinearLayoutManager
import androidx.recyclerview.widget.ListAdapter
import androidx.recyclerview.widget.RecyclerView
import com.google.android.material.bottomnavigation.BottomNavigationView
import com.reallive.android.R
import com.reallive.android.config.AppConfig
import com.reallive.android.data.CameraRepository
import com.reallive.android.network.ApiFactory
import com.reallive.android.network.AlertDto
import com.reallive.android.ui.common.MainTabNavigation
import com.reallive.android.ui.auth.AuthGuard
import com.reallive.android.ui.auth.LoginActivity
import com.reallive.android.ui.watch.EventDetailActivity
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import retrofit2.HttpException
import java.time.Instant
import java.time.OffsetDateTime
import java.time.format.DateTimeParseException
import java.time.ZoneOffset
import java.time.format.DateTimeFormatter
import java.util.Locale

class NotificationsActivity : AppCompatActivity() {
    private lateinit var adapter: NotificationAdapter
    private lateinit var appConfig: AppConfig
    private lateinit var repository: CameraRepository
    private lateinit var pageTitleText: TextView
    private lateinit var emptyText: TextView
    private lateinit var filterTypeChip: TextView
    private lateinit var filterStatusChip: TextView
    private lateinit var filterTimeChip: TextView
    private lateinit var filterSearchChip: TextView
    private lateinit var filterClearChip: TextView
    private var currentAlerts: List<NotificationEntry> = emptyList()
    private var currentTypeGroup: AlertTypeGroup = AlertTypeGroup.ALL
    private var currentStatus: AlertStatus = AlertStatus.ALL
    private var currentTimeRange: AlertTimeRange = AlertTimeRange.ALL
    private var currentQuery: String? = null
    private val prefs by lazy { getSharedPreferences("reallive_alert_filters", MODE_PRIVATE) }
    private var searchJob: Job? = null

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        appConfig = AppConfig(this)
        if (appConfig.getToken().isNullOrBlank()) {
            redirectToLogin()
            return
        }
        repository = CameraRepository(ApiFactory.createAuthorized(appConfig))
        setContentView(R.layout.activity_notifications)
        restoreFilters()
        pageTitleText = findViewById(R.id.notifications_page_title)
        emptyText = findViewById(R.id.notifications_empty)
        filterTypeChip = findViewById(R.id.notifications_filter_type)
        filterStatusChip = findViewById(R.id.notifications_filter_status)
        filterTimeChip = findViewById(R.id.notifications_filter_time)
        filterSearchChip = findViewById(R.id.notifications_filter_search)
        filterClearChip = findViewById(R.id.notifications_filter_clear)
        emptyText.visibility = View.GONE
        findViewById<View>(R.id.notifications_mark_read).setOnClickListener {
            markAllRead()
        }
        findViewById<View>(R.id.notifications_filter).setOnClickListener {
            openFilterDialog()
        }
        filterTypeChip.setOnClickListener { openFilterDialog() }
        filterStatusChip.setOnClickListener { openFilterDialog() }
        filterTimeChip.setOnClickListener { openTimeDialog() }
        filterSearchChip.setOnClickListener { openSearchDialog() }
        filterClearChip.setOnClickListener { clearFilters() }
        applyLocalizedTexts(appConfig.getAppLanguage())

        val recycler = findViewById<RecyclerView>(R.id.notifications_recycler)
        adapter = NotificationAdapter { openEventDetail(it) }
        recycler.layoutManager = LinearLayoutManager(this)
        recycler.adapter = adapter

        MainTabNavigation.setup(
            activity = this,
            bottomNav = findViewById<BottomNavigationView>(R.id.bottom_nav),
            currentTab = MainTabNavigation.TAB_ALERTS,
        )
    }

    override fun onStart() {
        super.onStart()
        if (appConfig.shouldRequireReauth()) {
            redirectToLogin(forceReauth = true)
            return
        }
        appConfig.markAuthenticated()
        loadAlerts()
    }

    override fun onStop() {
        super.onStop()
        appConfig.markAppBackgrounded()
    }

    private fun openEventDetail(entry: NotificationEntry) {
        if (entry.cameraId <= 0L) {
            Toast.makeText(
                this,
                if (isChineseLanguage(appConfig.getAppLanguage())) "该告警没有可用摄像头。" else "Camera not available for this alert.",
                Toast.LENGTH_SHORT,
            ).show()
            return
        }
        lifecycleScope.launch {
            try {
                if (entry.isUnread && entry.alertId > 0) {
                    withContext(Dispatchers.IO) { repository.markAlertRead(entry.alertId) }
                }
            } catch (_: Exception) {
            } finally {
                startActivity(
                    Intent(this@NotificationsActivity, EventDetailActivity::class.java).apply {
                        putExtra(EventDetailActivity.EXTRA_EVENT_TYPE, entry.eventType)
                        putExtra(EventDetailActivity.EXTRA_EVENT_TS, entry.eventTsMs)
                        putExtra(EventDetailActivity.EXTRA_EVENT_SCORE, 0.0)
                        putExtra(
                            EventDetailActivity.EXTRA_CAMERA_NAME,
                            entry.cameraName ?: if (isChineseLanguage(appConfig.getAppLanguage())) "摄像头" else "Camera",
                        )
                        putExtra(EventDetailActivity.EXTRA_CAMERA_ID, entry.cameraId)
                    },
                )
            }
        }
    }

    private fun loadAlerts() {
        lifecycleScope.launch {
            try {
                val alerts = withContext(Dispatchers.IO) {
                    repository.getAlerts(
                        limit = 50,
                        offset = 0,
                        typeGroup = currentTypeGroup.apiValue,
                        status = currentStatus.apiValue,
                        query = currentQuery?.takeIf { it.isNotBlank() },
                        since = currentTimeRange.sinceIso(),
                        until = currentTimeRange.untilIso(),
                    )
                }
                currentAlerts = alerts.map { mapAlert(it) }
                adapter.submitList(currentAlerts)
                emptyText.visibility = if (currentAlerts.isEmpty()) View.VISIBLE else View.GONE
                updateFilterChips()
            } catch (ex: Exception) {
                if (ex is HttpException && ex.code() == 401) {
                    val valid = withContext(Dispatchers.IO) { AuthGuard.isSessionValid(appConfig) }
                    if (!valid) {
                        appConfig.clearAuth()
                        redirectToLogin()
                    }
                    return@launch
                }
                emptyText.visibility = View.VISIBLE
                Toast.makeText(
                    this@NotificationsActivity,
                    if (isChineseLanguage(appConfig.getAppLanguage())) "加载告警失败。" else "Failed to load alerts.",
                    Toast.LENGTH_SHORT,
                ).show()
            }
        }
    }

    private fun mapAlert(alert: AlertDto): NotificationEntry {
        val title = alert.title.ifBlank {
            alert.type.ifBlank {
                if (isChineseLanguage(appConfig.getAppLanguage())) "告警" else "Alert"
            }
        }
        val subtitle = alert.description ?: ""
        val timestamp = alert.event_ts_ms ?: parseCreatedAt(alert.created_at) ?: System.currentTimeMillis()
        val timeText = DateUtils.getRelativeTimeSpanString(
            timestamp,
            System.currentTimeMillis(),
            DateUtils.MINUTE_IN_MILLIS,
            DateUtils.FORMAT_ABBREV_RELATIVE,
        ).toString()
        val level = when {
            alert.type.contains("person", true) -> Level.WARNING
            alert.type.contains("motion", true) -> Level.WARNING
            alert.type.contains("offline", true) -> Level.ERROR
            alert.type.contains("disconnect", true) -> Level.ERROR
            else -> Level.INFO
        }
        return NotificationEntry(
            id = alert.id.toString(),
            title = if (alert.camera_name.isNullOrBlank()) title else "$title - ${alert.camera_name}",
            subtitle = subtitle,
            time = timeText,
            cameraId = alert.camera_id ?: -1L,
            cameraName = alert.camera_name,
            eventType = alert.type.ifBlank { "event" },
            eventTsMs = timestamp,
            level = level,
            alertId = alert.id,
            isUnread = (alert.status ?: "new").equals("new", ignoreCase = true),
        )
    }

    private fun parseCreatedAt(value: String?): Long? {
        if (value.isNullOrBlank()) return null
        return try {
            Instant.parse(value).toEpochMilli()
        } catch (_: DateTimeParseException) {
            runCatching { OffsetDateTime.parse(value).toInstant().toEpochMilli() }.getOrNull()
        }
    }

    private fun markAllRead() {
        if (currentAlerts.isEmpty()) return
        lifecycleScope.launch {
            try {
                withContext(Dispatchers.IO) { markAllReadByFilter() }
                loadAlerts()
            } catch (_: Exception) {
            }
        }
    }

    private suspend fun markAllReadByFilter() {
        val collectedIds = mutableListOf<Long>()
        var offset = 0
        val limit = 50
        while (true) {
            val page = repository.getAlerts(
                limit = limit,
                offset = offset,
                typeGroup = currentTypeGroup.apiValue,
                status = currentStatus.apiValue,
                query = currentQuery?.takeIf { it.isNotBlank() },
                since = currentTimeRange.sinceIso(),
                until = currentTimeRange.untilIso(),
            )
            if (page.isEmpty()) break
            page.forEach { alert ->
                val id = alert.id
                if (id > 0) collectedIds.add(id)
            }
            if (page.size < limit) break
            offset += limit
        }
        if (collectedIds.isEmpty()) return
        val batchSize = 100
        var idx = 0
        while (idx < collectedIds.size) {
            val batch = collectedIds.subList(idx, kotlin.math.min(idx + batchSize, collectedIds.size))
            repository.markAlertsRead(batch)
            idx += batchSize
        }
    }

    private fun openFilterDialog() {
        val typeLabels = AlertTypeGroup.values().map { alertTypeLabel(it) }.toTypedArray()
        val typeIndex = AlertTypeGroup.values().indexOf(currentTypeGroup).coerceAtLeast(0)
        com.google.android.material.dialog.MaterialAlertDialogBuilder(this)
            .setTitle(if (isChineseLanguage(appConfig.getAppLanguage())) "告警类型" else "Alert Type")
            .setSingleChoiceItems(typeLabels, typeIndex) { dialog, which ->
                currentTypeGroup = AlertTypeGroup.values()[which]
                dialog.dismiss()
                persistFilters()
                openStatusDialog()
            }
            .setNegativeButton(if (isChineseLanguage(appConfig.getAppLanguage())) "取消" else "Cancel", null)
            .show()
    }

    private fun openStatusDialog() {
        val statusLabels = AlertStatus.values().map { alertStatusLabel(it) }.toTypedArray()
        val statusIndex = AlertStatus.values().indexOf(currentStatus).coerceAtLeast(0)
        com.google.android.material.dialog.MaterialAlertDialogBuilder(this)
            .setTitle(if (isChineseLanguage(appConfig.getAppLanguage())) "告警状态" else "Alert Status")
            .setSingleChoiceItems(statusLabels, statusIndex) { dialog, which ->
                currentStatus = AlertStatus.values()[which]
                dialog.dismiss()
                updateFilterChips()
                persistFilters()
                loadAlerts()
            }
            .setNegativeButton(if (isChineseLanguage(appConfig.getAppLanguage())) "取消" else "Cancel", null)
            .show()
    }

    private fun openTimeDialog() {
        val labels = AlertTimeRange.values().map { alertTimeRangeLabel(it) }.toTypedArray()
        val index = AlertTimeRange.values().indexOf(currentTimeRange).coerceAtLeast(0)
        com.google.android.material.dialog.MaterialAlertDialogBuilder(this)
            .setTitle(if (isChineseLanguage(appConfig.getAppLanguage())) "时间范围" else "Time Range")
            .setSingleChoiceItems(labels, index) { dialog, which ->
                currentTimeRange = AlertTimeRange.values()[which]
                dialog.dismiss()
                updateFilterChips()
                persistFilters()
                loadAlerts()
            }
            .setNegativeButton(if (isChineseLanguage(appConfig.getAppLanguage())) "取消" else "Cancel", null)
            .show()
    }

    private fun openSearchDialog() {
        val input = EditText(this).apply {
            hint = if (isChineseLanguage(appConfig.getAppLanguage())) "搜索告警" else "Search alerts"
            setText(currentQuery ?: "")
        }
        com.google.android.material.dialog.MaterialAlertDialogBuilder(this)
            .setTitle(if (isChineseLanguage(appConfig.getAppLanguage())) "搜索" else "Search")
            .setView(input)
            .setPositiveButton(if (isChineseLanguage(appConfig.getAppLanguage())) "应用" else "Apply") { _, _ ->
                val query = input.text?.toString()?.trim()
                applySearchQuery(query)
            }
            .setNegativeButton(if (isChineseLanguage(appConfig.getAppLanguage())) "取消" else "Cancel", null)
            .show()
    }

    private fun applySearchQuery(raw: String?) {
        currentQuery = raw?.takeIf { it.isNotBlank() }
        updateFilterChips()
        persistFilters()
        searchJob?.cancel()
        searchJob = lifecycleScope.launch {
            delay(350)
            loadAlerts()
        }
    }

    private fun updateFilterChips() {
        val zh = isChineseLanguage(appConfig.getAppLanguage())
        val typePrefix = if (zh) "类型" else "Type"
        val statusPrefix = if (zh) "状态" else "Status"
        val timePrefix = if (zh) "时间" else "Time"
        val searchLabel = if (zh) "搜索" else "Search"
        filterTypeChip.text = "$typePrefix: ${alertTypeLabel(currentTypeGroup)}"
        filterStatusChip.text = "$statusPrefix: ${alertStatusLabel(currentStatus)}"
        filterTimeChip.text = "$timePrefix: ${alertTimeRangeLabel(currentTimeRange)}"
        filterSearchChip.text = if (currentQuery.isNullOrBlank()) searchLabel else "$searchLabel: ${currentQuery}"
    }

    private fun clearFilters() {
        currentTypeGroup = AlertTypeGroup.ALL
        currentStatus = AlertStatus.ALL
        currentTimeRange = AlertTimeRange.ALL
        currentQuery = null
        updateFilterChips()
        persistFilters()
        loadAlerts()
    }

    private fun redirectToLogin(forceReauth: Boolean = false) {
        startActivity(
            Intent(this, LoginActivity::class.java).apply {
                if (forceReauth) putExtra(LoginActivity.EXTRA_FORCE_REAUTH, true)
            },
        )
        finish()
    }

    private fun applyLocalizedTexts(languageCode: String) {
        val zh = isChineseLanguage(languageCode)
        pageTitleText.text = if (zh) "告警" else "Alerts"
        emptyText.text = if (zh) "暂无告警" else "No alerts"
        filterClearChip.text = if (zh) "清除" else "Clear"
        updateFilterChips()
    }

    private fun alertTypeLabel(group: AlertTypeGroup): String {
        val zh = isChineseLanguage(appConfig.getAppLanguage())
        return when (group) {
            AlertTypeGroup.ALL -> if (zh) "全部" else "All"
            AlertTypeGroup.MOTION -> if (zh) "移动" else "Motion"
            AlertTypeGroup.OFFLINE -> if (zh) "离线" else "Offline"
            AlertTypeGroup.ALARM -> if (zh) "警报" else "Alarm"
            AlertTypeGroup.SYSTEM -> if (zh) "系统" else "System"
        }
    }

    private fun alertStatusLabel(status: AlertStatus): String {
        val zh = isChineseLanguage(appConfig.getAppLanguage())
        return when (status) {
            AlertStatus.ALL -> if (zh) "全部" else "All"
            AlertStatus.NEW -> if (zh) "新告警" else "New"
            AlertStatus.READ -> if (zh) "已读" else "Read"
            AlertStatus.RESOLVED -> if (zh) "已处理" else "Resolved"
        }
    }

    private fun alertTimeRangeLabel(range: AlertTimeRange): String {
        val zh = isChineseLanguage(appConfig.getAppLanguage())
        return when (range) {
            AlertTimeRange.LAST_24H -> if (zh) "24小时" else "24h"
            AlertTimeRange.LAST_7D -> if (zh) "7天" else "7d"
            AlertTimeRange.LAST_30D -> if (zh) "30天" else "30d"
            AlertTimeRange.ALL -> if (zh) "全部" else "All"
        }
    }

    private fun isChineseLanguage(languageCode: String?): Boolean {
        if (languageCode.isNullOrBlank()) return false
        return languageCode.lowercase(Locale.US).startsWith("zh")
    }

    private enum class Level { INFO, WARNING, ERROR }

    private enum class AlertTypeGroup(val apiValue: String?) {
        ALL(null),
        MOTION("motion"),
        OFFLINE("offline"),
        ALARM("alarm"),
        SYSTEM("system"),
    }

    private enum class AlertStatus(val apiValue: String?) {
        ALL(null),
        NEW("new"),
        READ("read"),
        RESOLVED("resolved"),
    }

    private enum class AlertTimeRange(val hours: Long?) {
        LAST_24H(24),
        LAST_7D(24 * 7),
        LAST_30D(24 * 30),
        ALL(null),
    }

    private fun AlertTimeRange.sinceIso(): String? {
        val h = hours ?: return null
        val since = Instant.now().minusSeconds(h * 3600)
        return OffsetDateTime.ofInstant(since, ZoneOffset.UTC).format(DateTimeFormatter.ISO_OFFSET_DATE_TIME)
    }

    private fun AlertTimeRange.untilIso(): String? {
        if (hours == null) return null
        val now = Instant.now()
        return OffsetDateTime.ofInstant(now, ZoneOffset.UTC).format(DateTimeFormatter.ISO_OFFSET_DATE_TIME)
    }

    private fun persistFilters() {
        prefs.edit()
            .putString("type", currentTypeGroup.name)
            .putString("status", currentStatus.name)
            .putString("time", currentTimeRange.name)
            .putString("query", currentQuery)
            .apply()
    }

    private fun restoreFilters() {
        currentTypeGroup = runCatching {
            AlertTypeGroup.valueOf(prefs.getString("type", AlertTypeGroup.ALL.name) ?: AlertTypeGroup.ALL.name)
        }.getOrDefault(AlertTypeGroup.ALL)
        currentStatus = runCatching {
            AlertStatus.valueOf(prefs.getString("status", AlertStatus.ALL.name) ?: AlertStatus.ALL.name)
        }.getOrDefault(AlertStatus.ALL)
        currentTimeRange = runCatching {
            AlertTimeRange.valueOf(prefs.getString("time", AlertTimeRange.ALL.name) ?: AlertTimeRange.ALL.name)
        }.getOrDefault(AlertTimeRange.ALL)
        currentQuery = prefs.getString("query", null)?.takeIf { it.isNotBlank() }
    }

    private data class NotificationEntry(
        val id: String,
        val title: String,
        val subtitle: String,
        val time: String,
        val cameraId: Long,
        val cameraName: String?,
        val eventType: String,
        val eventTsMs: Long,
        val level: Level,
        val alertId: Long = -1L,
        val isUnread: Boolean = false,
    )

    private class NotificationAdapter(
        private val onClick: (NotificationEntry) -> Unit,
    ) : ListAdapter<NotificationEntry, NotificationAdapter.Holder>(Diff()) {
        override fun onCreateViewHolder(parent: ViewGroup, viewType: Int): Holder {
            val view = LayoutInflater.from(parent.context).inflate(R.layout.item_notification, parent, false)
            return Holder(view, onClick)
        }

        override fun onBindViewHolder(holder: Holder, position: Int) = holder.bind(getItem(position))

        class Holder(view: View, private val onClick: (NotificationEntry) -> Unit) : RecyclerView.ViewHolder(view) {
            private val iconContainer: FrameLayout = view.findViewById(R.id.notification_icon_container)
            private val iconView: ImageView = view.findViewById(R.id.notification_icon)
            private val titleText: TextView = view.findViewById(R.id.notification_title)
            private val subtitleText: TextView = view.findViewById(R.id.notification_subtitle)
            private val timeText: TextView = view.findViewById(R.id.notification_time)
            private val thumbView: FrameLayout = view.findViewById(R.id.notification_thumb)
            private val unreadDot: View = view.findViewById(R.id.notification_unread_dot)
            private var entry: NotificationEntry? = null

            init {
                itemView.setOnClickListener { entry?.let(onClick) }
            }

            fun bind(item: NotificationEntry) {
                entry = item
                titleText.text = item.title
                subtitleText.text = item.subtitle
                timeText.text = item.time
                unreadDot.visibility = if (item.isUnread) View.VISIBLE else View.GONE
                val title = item.title.lowercase()
                when {
                    title.contains("person") -> {
                        iconView.setImageResource(R.drawable.ic_rl_person_alert_24)
                        applyIconStyle(R.color.rl_error, R.color.rl_error)
                        thumbView.visibility = View.VISIBLE
                    }
                    title.contains("motion") -> {
                        iconView.setImageResource(R.drawable.ic_rl_directions_run_24)
                        applyIconStyle(R.color.rl_warning, R.color.rl_warning)
                        thumbView.visibility = View.VISIBLE
                    }
                    title.contains("offline") -> {
                        iconView.setImageResource(R.drawable.ic_rl_videocam_off_24)
                        applyIconStyle(R.color.rl_error, R.color.rl_error)
                        thumbView.visibility = View.GONE
                    }
                    title.contains("firmware") -> {
                        iconView.setImageResource(R.drawable.ic_rl_update_24)
                        applyIconStyle(R.color.rl_brand, R.color.rl_brand)
                        thumbView.visibility = View.GONE
                    }
                    title.contains("cloud") -> {
                        iconView.setImageResource(R.drawable.ic_rl_cloud_done_24)
                        applyIconStyle(R.color.rl_brand, R.color.rl_brand)
                        thumbView.visibility = View.GONE
                    }
                    else -> {
                        iconView.setImageResource(R.drawable.ic_rl_notifications_24)
                        applyIconStyle(R.color.rl_secondary, R.color.rl_secondary)
                        thumbView.visibility = View.GONE
                    }
                }
            }

            private fun applyIconStyle(bgColorRes: Int, tintColorRes: Int) {
                val bgColor = ContextCompat.getColor(itemView.context, bgColorRes)
                val drawable = GradientDrawable().apply {
                    shape = GradientDrawable.OVAL
                    setColor((bgColor and 0x00FFFFFF) or 0x1F000000)
                }
                iconContainer.background = drawable
                iconView.setColorFilter(ContextCompat.getColor(itemView.context, tintColorRes))
            }
        }

        private class Diff : DiffUtil.ItemCallback<NotificationEntry>() {
            override fun areItemsTheSame(oldItem: NotificationEntry, newItem: NotificationEntry): Boolean = oldItem.id == newItem.id
            override fun areContentsTheSame(oldItem: NotificationEntry, newItem: NotificationEntry): Boolean = oldItem == newItem
        }
    }
}
