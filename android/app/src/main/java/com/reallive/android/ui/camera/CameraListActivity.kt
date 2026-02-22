package com.reallive.android.ui.camera

import android.content.Intent
import android.content.res.ColorStateList
import android.graphics.Color
import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import androidx.core.content.ContextCompat
import androidx.lifecycle.lifecycleScope
import androidx.recyclerview.widget.DiffUtil
import androidx.recyclerview.widget.LinearLayoutManager
import androidx.recyclerview.widget.ListAdapter
import androidx.recyclerview.widget.RecyclerView
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import com.reallive.android.R
import com.reallive.android.config.AppConfig
import com.reallive.android.data.CameraRepository
import com.reallive.android.network.ApiClient
import com.reallive.android.network.CameraDto
import com.reallive.android.ui.watch.WatchActivity
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import retrofit2.HttpException
import java.util.Locale

class CameraListActivity : AppCompatActivity() {
    private lateinit var appConfig: AppConfig
    private lateinit var repository: CameraRepository
    private lateinit var statusText: TextView
    private lateinit var adapter: CameraListAdapter
    private lateinit var filterAll: TextView
    private lateinit var filterOnline: TextView
    private lateinit var filterOffline: TextView
    private lateinit var filterRecording: TextView

    private val cameras = mutableListOf<CameraDto>()
    private var currentFilter = Filter.ALL
    private var isZh: Boolean = false

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        appConfig = AppConfig(this)
        isZh = isChineseLanguage(appConfig.getAppLanguage())
        if (appConfig.getToken().isNullOrBlank()) {
            finish()
            return
        }
        repository = CameraRepository(ApiClient.create(appConfig.getBaseUrl(), appConfig::getToken))
        setContentView(R.layout.activity_camera_list)
        findViewById<TextView>(R.id.camera_list_page_title).text = tr("Camera Management", "摄像头管理")

        findViewById<View>(R.id.camera_list_back).setOnClickListener { finish() }
        findViewById<View>(R.id.camera_list_sort).setOnClickListener { }
        statusText = findViewById(R.id.camera_list_status)

        adapter = CameraListAdapter(
            isZh = isZh,
            onClick = { openWatch(it) },
            onLongClick = { openCameraSettings(it) },
        )
        val recycler = findViewById<RecyclerView>(R.id.camera_list_recycler)
        recycler.layoutManager = LinearLayoutManager(this)
        recycler.adapter = adapter

        filterAll = findViewById(R.id.camera_filter_all)
        filterOnline = findViewById(R.id.camera_filter_online)
        filterOffline = findViewById(R.id.camera_filter_offline)
        filterRecording = findViewById(R.id.camera_filter_recording)
        updateFilterChipText(filterAll, filterOnline, filterOffline, filterRecording)
        fun selectFilter(filter: Filter) {
            currentFilter = filter
            applyFilterStyle(filterAll, filter == Filter.ALL)
            applyFilterStyle(filterOnline, filter == Filter.ONLINE)
            applyFilterStyle(filterOffline, filter == Filter.OFFLINE)
            applyFilterStyle(filterRecording, filter == Filter.RECORDING)
            applyFilter()
            updateFilterChipText(filterAll, filterOnline, filterOffline, filterRecording)
        }
        filterAll.setOnClickListener { selectFilter(Filter.ALL) }
        filterOnline.setOnClickListener { selectFilter(Filter.ONLINE) }
        filterOffline.setOnClickListener { selectFilter(Filter.OFFLINE) }
        filterRecording.setOnClickListener { selectFilter(Filter.RECORDING) }
        selectFilter(Filter.ALL)
    }

    override fun onStart() {
        super.onStart()
        loadCameras()
    }

    private fun loadCameras() {
        lifecycleScope.launch {
            try {
                val list = withContext(Dispatchers.IO) { repository.listCameras() }
                cameras.clear()
                cameras.addAll(list)
                applyFilter()
                updateFilterChipText(filterAll, filterOnline, filterOffline, filterRecording)
            } catch (ex: Exception) {
                if (ex is HttpException && ex.code() == 401) {
                    appConfig.clearAuth()
                    finish()
                }
            }
        }
    }

    private fun applyFilter() {
        val filtered = cameras.filter { camera ->
            val status = (camera.status ?: "").lowercase(Locale.US)
            when (currentFilter) {
                Filter.ALL -> true
                Filter.ONLINE -> status == "online" || status == "streaming"
                Filter.OFFLINE -> status == "offline"
                Filter.RECORDING -> status == "streaming"
            }
        }
        adapter.submitList(filtered)
        statusText.text = if (isZh) {
            "${filtered.size}/${cameras.size} 个摄像头"
        } else {
            "${filtered.size}/${cameras.size} cameras"
        }
    }

    private fun updateFilterChipText(
        filterAll: TextView,
        filterOnline: TextView,
        filterOffline: TextView,
        filterRecording: TextView,
    ) {
        val total = cameras.size
        val online = cameras.count {
            val s = (it.status ?: "").lowercase(Locale.US)
            s == "online" || s == "streaming"
        }
        val offline = cameras.count { (it.status ?: "").lowercase(Locale.US) == "offline" }
        filterAll.text = if (isZh) "全部 ($total)" else "All ($total)"
        filterOnline.text = if (isZh) "在线 ($online)" else "Online ($online)"
        filterOffline.text = if (isZh) "离线 ($offline)" else "Offline ($offline)"
        filterRecording.text = if (isZh) "录制中" else "Recording"
    }

    private fun applyFilterStyle(view: TextView, selected: Boolean) {
        view.setBackgroundResource(if (selected) R.drawable.bg_btn_tonal else R.drawable.bg_btn_outline)
        val color = if (selected) {
            Color.parseColor("#E5DFF9")
        } else {
            ContextCompat.getColor(this, R.color.auth_on_surface_variant)
        }
        view.setTextColor(color)
        view.compoundDrawableTintList = ColorStateList.valueOf(color)
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

    private fun openCameraSettings(camera: CameraDto) {
        startActivity(
            Intent(this, CameraSettingsActivity::class.java).apply {
                putExtra(CameraSettingsActivity.EXTRA_CAMERA_ID, camera.id)
                putExtra(CameraSettingsActivity.EXTRA_CAMERA_NAME, camera.name)
            },
        )
    }

    private fun tr(en: String, zh: String): String = if (isZh) zh else en

    private fun isChineseLanguage(languageCode: String?): Boolean {
        if (languageCode.isNullOrBlank()) return false
        return languageCode.lowercase(Locale.US).startsWith("zh")
    }

    private enum class Filter {
        ALL,
        ONLINE,
        OFFLINE,
        RECORDING,
    }
}

private class CameraListAdapter(
    private val isZh: Boolean,
    private val onClick: (CameraDto) -> Unit,
    private val onLongClick: (CameraDto) -> Unit,
) : ListAdapter<CameraDto, CameraListAdapter.CameraViewHolder>(DiffCallback()) {
    override fun onCreateViewHolder(parent: ViewGroup, viewType: Int): CameraViewHolder {
        val view = LayoutInflater.from(parent.context)
            .inflate(R.layout.item_camera_manage_row, parent, false)
        return CameraViewHolder(view, isZh, onClick, onLongClick)
    }

    override fun onBindViewHolder(holder: CameraViewHolder, position: Int) {
        holder.bind(getItem(position))
    }

    class CameraViewHolder(
        itemView: View,
        private val isZh: Boolean,
        private val onClick: (CameraDto) -> Unit,
        private val onLongClick: (CameraDto) -> Unit,
    ) : RecyclerView.ViewHolder(itemView) {
        private val nameText: TextView = itemView.findViewById(R.id.camera_manage_name)
        private val metaText: TextView = itemView.findViewById(R.id.camera_manage_meta)
        private val badgeText: TextView = itemView.findViewById(R.id.camera_manage_status)
        private val badgeDot: View = itemView.findViewById(R.id.camera_manage_dot)
        private val editButton: View = itemView.findViewById(R.id.camera_manage_edit)
        private val deleteButton: View = itemView.findViewById(R.id.camera_manage_delete)
        private var camera: CameraDto? = null

        init {
            itemView.setOnClickListener { camera?.let(onClick) }
            itemView.setOnLongClickListener {
                camera?.let(onLongClick)
                true
            }
            editButton.setOnClickListener { camera?.let(onLongClick) }
            deleteButton.setOnClickListener {
                camera?.let {
                    MaterialAlertDialogBuilder(itemView.context)
                        .setTitle(if (isZh) "移除摄像头" else "Remove Camera")
                        .setMessage(if (isZh) "移除 ${it.name}？" else "Remove ${it.name}?")
                        .setPositiveButton(if (isZh) "移除" else "Remove") { _, _ -> }
                        .setNegativeButton(if (isZh) "取消" else "Cancel", null)
                        .show()
                }
            }
        }

        fun bind(item: CameraDto) {
            camera = item
            nameText.text = item.name
            metaText.text = "${item.resolution ?: if (isZh) "自动" else "auto"} • ${item.stream_key.take(10)}"
            val status = item.status ?: "offline"
            val statusLower = status.lowercase(Locale.US)
            badgeText.text = when (statusLower) {
                "streaming" -> if (isZh) "录制中" else "streaming"
                "online" -> if (isZh) "在线" else "online"
                else -> if (isZh) "离线" else "offline"
            }
            if (statusLower == "streaming") {
                badgeText.setTextColor(ContextCompat.getColor(itemView.context, R.color.rl_success))
                badgeDot.setBackgroundResource(R.drawable.bg_status_dot_orange)
            } else if (statusLower == "online") {
                badgeText.setTextColor(ContextCompat.getColor(itemView.context, R.color.rl_success))
                badgeDot.setBackgroundResource(R.drawable.bg_status_dot_green)
            } else {
                badgeText.setTextColor(ContextCompat.getColor(itemView.context, R.color.rl_error))
                badgeDot.setBackgroundResource(R.drawable.bg_status_dot_red)
            }
        }
    }

    private class DiffCallback : DiffUtil.ItemCallback<CameraDto>() {
        override fun areItemsTheSame(oldItem: CameraDto, newItem: CameraDto): Boolean {
            return oldItem.id == newItem.id
        }

        override fun areContentsTheSame(oldItem: CameraDto, newItem: CameraDto): Boolean {
            return oldItem == newItem
        }
    }
}
