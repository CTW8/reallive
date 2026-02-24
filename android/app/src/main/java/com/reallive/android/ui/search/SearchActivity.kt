package com.reallive.android.ui.search

import android.content.Intent
import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.EditText
import android.widget.ImageView
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import androidx.core.content.ContextCompat
import androidx.core.widget.doOnTextChanged
import androidx.lifecycle.lifecycleScope
import androidx.recyclerview.widget.DiffUtil
import androidx.recyclerview.widget.LinearLayoutManager
import androidx.recyclerview.widget.ListAdapter
import androidx.recyclerview.widget.RecyclerView
import com.reallive.android.R
import com.reallive.android.config.AppConfig
import com.reallive.android.data.CameraRepository
import com.reallive.android.network.ApiFactory
import com.reallive.android.network.AlertDto
import com.reallive.android.network.CameraDto
import com.reallive.android.ui.auth.AuthGuard
import com.reallive.android.ui.auth.LoginActivity
import com.reallive.android.ui.watch.WatchActivity
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import retrofit2.HttpException
import java.time.Instant
import java.time.OffsetDateTime
import java.time.ZoneOffset
import java.time.format.DateTimeFormatter
import java.util.Locale

class SearchActivity : AppCompatActivity() {
    private lateinit var adapter: SearchResultAdapter
    private lateinit var emptyState: View
    private lateinit var resultsLabel: TextView
    private lateinit var input: EditText

    private var query = ""
    private lateinit var repository: CameraRepository
    private lateinit var appConfig: AppConfig
    private var cameras: List<CameraDto> = emptyList()
    private var searchJob: Job? = null
    private var isZh: Boolean = false

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        appConfig = AppConfig(this)
        isZh = isChineseLanguage(appConfig.getAppLanguage())
        if (appConfig.getToken().isNullOrBlank()) {
            redirectToLogin()
            return
        }
        repository = CameraRepository(ApiFactory.createAuthorized(appConfig))
        setContentView(R.layout.activity_search)

        emptyState = findViewById(R.id.search_empty_state)
        resultsLabel = findViewById(R.id.search_results_label)
        applyLocalizedTexts()

        findViewById<android.view.View>(R.id.search_back).setOnClickListener { finish() }

        val recycler = findViewById<RecyclerView>(R.id.search_recycler)
        adapter = SearchResultAdapter { item -> openWatch(item) }
        recycler.layoutManager = LinearLayoutManager(this)
        recycler.adapter = adapter

        input = findViewById(R.id.search_input)
        input.doOnTextChanged { text, _, _, _ ->
            query = text?.toString().orEmpty()
            scheduleSearch()
        }

        findViewById<android.view.View>(R.id.search_clear).setOnClickListener {
            input.setText("")
        }
        findViewById<android.view.View>(R.id.search_recent_front).setOnClickListener { input.setText("Front") }
        findViewById<android.view.View>(R.id.search_recent_garage).setOnClickListener { input.setText("Garage") }
        findViewById<android.view.View>(R.id.search_recent_person).setOnClickListener { input.setText("Person") }
    }

    override fun onStart() {
        super.onStart()
        loadCameras()
    }

    private fun loadCameras() {
        lifecycleScope.launch {
            try {
                cameras = withContext(Dispatchers.IO) { repository.listCameras() }
                applyFilter()
            } catch (ex: Exception) {
                if (ex is HttpException && ex.code() == 401) {
                    val valid = withContext(Dispatchers.IO) { AuthGuard.isSessionValid(appConfig) }
                    if (!valid) {
                        appConfig.clearAuth()
                        redirectToLogin()
                    } else {
                        loadCameras()
                    }
                }
            }
        }
    }

    private fun openWatch(item: SearchItem) {
        if (item.cameraId <= 0L) return
        startActivity(
            Intent(this, WatchActivity::class.java).apply {
                putExtra(WatchActivity.EXTRA_CAMERA_ID, item.cameraId)
                putExtra(
                    WatchActivity.EXTRA_CAMERA_NAME,
                    item.cameraName ?: item.title,
                )
            },
        )
    }

    private fun scheduleSearch() {
        searchJob?.cancel()
        searchJob = lifecycleScope.launch {
            delay(300)
            applyFilter()
        }
    }

    private fun applyFilter() {
        val q = query.trim().lowercase(Locale.US)
        lifecycleScope.launch {
            val cameraMatches = cameras.filter {
                if (q.isBlank()) return@filter true
                it.name.lowercase(Locale.US).contains(q) || (it.resolution ?: "").lowercase(Locale.US).contains(q)
            }.map { camera ->
                SearchItem(
                    id = "camera-${camera.id}",
                    type = SearchType.CAMERA,
                    title = camera.name,
                    subtitle = "${tr("Camera", "摄像头")} · ${camera.resolution ?: tr("Auto", "自动")} · ${
                        statusLabel(camera.status ?: "offline")
                    }",
                    badge = "live",
                    cameraId = camera.id,
                    cameraName = camera.name,
                )
            }

            val alertMatches = if (q.isBlank()) {
                emptyList()
            } else {
                val since = OffsetDateTime.ofInstant(Instant.now().minusSeconds(24 * 3600), ZoneOffset.UTC)
                    .format(DateTimeFormatter.ISO_OFFSET_DATE_TIME)
                val alerts = withContext(Dispatchers.IO) {
                    repository.getAlerts(limit = 30, offset = 0, query = q, since = since)
                }
                alerts.map { alert -> mapAlert(alert) }
            }

            val result = cameraMatches + alertMatches
            adapter.submitList(result)
            val empty = result.isEmpty()
            emptyState.visibility = if (empty) View.VISIBLE else View.GONE
            resultsLabel.visibility = if (empty) View.GONE else View.VISIBLE
        }
    }

    private fun mapAlert(alert: AlertDto): SearchItem {
        val name = alert.camera_name ?: "Camera"
        val title = when {
            alert.type.contains("person", true) -> "${tr("Person", "人物")} - $name"
            alert.type.contains("motion", true) -> "${tr("Motion", "移动")} - $name"
            alert.type.contains("offline", true) -> "${tr("Offline", "离线")} - $name"
            else -> "${alert.title} - $name"
        }
        return SearchItem(
            id = "alert-${alert.id}",
            type = SearchType.EVENT,
            title = title,
            subtitle = "${tr("Event", "事件")} · ${statusLabel(alert.status ?: "new")}",
            badge = "event",
            cameraId = alert.camera_id ?: -1L,
            cameraName = alert.camera_name,
        )
    }

    private data class SearchItem(
        val id: String,
        val type: SearchType,
        val title: String,
        val subtitle: String,
        val badge: String,
        val cameraId: Long,
        val cameraName: String? = null,
    )

    private enum class SearchType {
        CAMERA,
        EVENT,
    }

    private class SearchResultAdapter(
        private val onClick: (SearchItem) -> Unit,
    ) : ListAdapter<SearchItem, SearchResultAdapter.SearchViewHolder>(DiffCallback()) {
        override fun onCreateViewHolder(parent: ViewGroup, viewType: Int): SearchViewHolder {
            val view = LayoutInflater.from(parent.context)
                .inflate(R.layout.item_search_result, parent, false)
            return SearchViewHolder(view, onClick)
        }

        override fun onBindViewHolder(holder: SearchViewHolder, position: Int) {
            holder.bind(getItem(position))
        }

        class SearchViewHolder(
            itemView: View,
            private val onClick: (SearchItem) -> Unit,
        ) : RecyclerView.ViewHolder(itemView) {
            private val iconView: ImageView = itemView.findViewById(R.id.search_item_icon)
            private val titleText: TextView = itemView.findViewById(R.id.search_item_title)
            private val subtitleText: TextView = itemView.findViewById(R.id.search_item_subtitle)
            private val badgeView: ImageView = itemView.findViewById(R.id.search_item_badge)
            private var item: SearchItem? = null

            init {
                itemView.setOnClickListener { item?.let(onClick) }
            }

        fun bind(item: SearchItem) {
            this.item = item
            titleText.text = item.title
            subtitleText.text = item.subtitle
            when (item.type) {
                SearchType.CAMERA -> {
                    iconView.setImageResource(R.drawable.ic_videocam_48)
                    iconView.setColorFilter(ContextCompat.getColor(itemView.context, R.color.rl_secondary))
                }
                SearchType.EVENT -> {
                    if (item.title.contains("Person", ignoreCase = true)) {
                        iconView.setImageResource(R.drawable.ic_rl_person_24)
                        iconView.setColorFilter(ContextCompat.getColor(itemView.context, R.color.rl_error))
                    } else {
                        iconView.setImageResource(R.drawable.ic_rl_directions_run_24)
                        iconView.setColorFilter(ContextCompat.getColor(itemView.context, R.color.rl_warning))
                    }
                }
            }
            badgeView.setColorFilter(ContextCompat.getColor(itemView.context, R.color.rl_text_secondary))
        }
    }

        private class DiffCallback : DiffUtil.ItemCallback<SearchItem>() {
            override fun areItemsTheSame(oldItem: SearchItem, newItem: SearchItem): Boolean = oldItem.id == newItem.id
            override fun areContentsTheSame(oldItem: SearchItem, newItem: SearchItem): Boolean = oldItem == newItem
        }
    }

    private fun redirectToLogin() {
        startActivity(Intent(this, LoginActivity::class.java))
        finish()
    }

    private fun applyLocalizedTexts() {
        findViewById<TextView>(R.id.search_page_title).text = tr("Search", "搜索")
        findViewById<TextView>(R.id.search_recent_title).text = tr("RECENT SEARCHES", "最近搜索")
        findViewById<TextView>(R.id.search_results_label).text = tr("RESULTS", "结果")
        findViewById<TextView>(R.id.search_recent_front).text = tr("Front Door", "前门")
        findViewById<TextView>(R.id.search_recent_garage).text = tr("Garage motion", "车库移动")
        findViewById<TextView>(R.id.search_recent_person).text = tr("Person alert", "人物告警")
        input = findViewById(R.id.search_input)
        input.hint = tr("Search cameras, events...", "搜索摄像头、事件...")
    }

    private fun statusLabel(raw: String): String {
        return when (raw.lowercase(Locale.US)) {
            "online" -> tr("Online", "在线")
            "offline" -> tr("Offline", "离线")
            "streaming" -> tr("Streaming", "直播中")
            "new" -> tr("New", "新告警")
            "read" -> tr("Read", "已读")
            "resolved" -> tr("Resolved", "已处理")
            else -> raw.replaceFirstChar { it.uppercase() }
        }
    }

    private fun tr(en: String, zh: String): String = if (isZh) zh else en

    private fun isChineseLanguage(languageCode: String?): Boolean {
        if (languageCode.isNullOrBlank()) return false
        return languageCode.lowercase(Locale.US).startsWith("zh")
    }
}
