package com.reallive.android.ui.watch.snapshot

import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.ImageView
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import androidx.lifecycle.lifecycleScope
import androidx.recyclerview.widget.DiffUtil
import androidx.recyclerview.widget.GridLayoutManager
import androidx.recyclerview.widget.ListAdapter
import androidx.recyclerview.widget.RecyclerView
import coil.load
import com.reallive.android.R
import com.reallive.android.config.AppConfig
import com.reallive.android.data.CameraRepository
import com.reallive.android.network.ApiClient
import com.reallive.android.network.HistoryThumbnailDto
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import retrofit2.HttpException
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

class SnapshotGalleryActivity : AppCompatActivity() {
    private lateinit var todayLabel: TextView
    private lateinit var yesterdayLabel: TextView
    private lateinit var todayAdapter: SnapshotAdapter
    private lateinit var yesterdayAdapter: SnapshotAdapter
    private var cameraName: String = "Camera"
    private var cameraId: Long = -1L
    private lateinit var repository: CameraRepository
    private val allItems = mutableListOf<SnapshotItem>()
    private val selectedIds = linkedSetOf<String>()
    private var selecting: Boolean = false
    private lateinit var snackbar: View
    private lateinit var snackbarText: TextView

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_snapshot_gallery)

        val appConfig = AppConfig(this)
        repository = CameraRepository(ApiClient.create(appConfig.getBaseUrl(), appConfig::getToken))
        cameraName = intent.getStringExtra(EXTRA_CAMERA_NAME) ?: "Front Door"
        cameraId = intent.getLongExtra(EXTRA_CAMERA_ID, -1L)

        findViewById<android.view.View>(R.id.snapshot_back).setOnClickListener { finish() }
        findViewById<TextView>(R.id.snapshot_camera_info).text = "$cameraName · ${allItems.size} photos"
        snackbar = findViewById(R.id.snapshot_snackbar)
        snackbarText = findViewById(R.id.snapshot_snackbar_message)

        todayLabel = findViewById(R.id.snapshot_today_label)
        yesterdayLabel = findViewById(R.id.snapshot_yesterday_label)
        todayAdapter = SnapshotAdapter(
            onClick = { item ->
                if (selecting) {
                    toggleSelection(item)
                } else {
                    openPlayback(item)
                }
            },
            isSelecting = { selecting },
            isSelected = { id -> selectedIds.contains(id) },
        )
        yesterdayAdapter = SnapshotAdapter(
            onClick = { item ->
                if (selecting) {
                    toggleSelection(item)
                } else {
                    openPlayback(item)
                }
            },
            isSelecting = { selecting },
            isSelected = { id -> selectedIds.contains(id) },
        )

        findViewById<RecyclerView>(R.id.snapshot_recycler_today).apply {
            layoutManager = GridLayoutManager(this@SnapshotGalleryActivity, 3)
            adapter = todayAdapter
        }
        findViewById<RecyclerView>(R.id.snapshot_recycler_yesterday).apply {
            layoutManager = GridLayoutManager(this@SnapshotGalleryActivity, 3)
            adapter = yesterdayAdapter
        }

        findViewById<android.view.View>(R.id.snapshot_filter_all).setOnClickListener {
            render(allItems)
        }
        findViewById<android.view.View>(R.id.snapshot_filter_today).setOnClickListener {
            render(allItems.filter { it.group == Group.TODAY })
        }
        findViewById<android.view.View>(R.id.snapshot_filter_week).setOnClickListener {
            render(allItems.filter { it.group == Group.WEEK || it.group == Group.TODAY })
        }
        findViewById<android.view.View>(R.id.snapshot_filter_month).setOnClickListener {
            render(allItems)
        }
        findViewById<View>(R.id.snapshot_select_toggle).setOnClickListener {
            selecting = !selecting
            if (!selecting) selectedIds.clear()
            refreshSelectionUi()
            renderCurrent()
        }
        findViewById<View>(R.id.snapshot_delete_toggle).setOnClickListener {
            if (selectedIds.isEmpty()) return@setOnClickListener
            val before = allItems.size
            allItems.removeAll { selectedIds.contains(it.id) }
            val removed = before - allItems.size
            selectedIds.clear()
            findViewById<TextView>(R.id.snapshot_camera_info).text = "$cameraName · ${allItems.size} photos"
            showSnackbar("$removed snapshots removed")
            renderCurrent()
        }
        findViewById<View>(R.id.snapshot_snackbar_action).setOnClickListener {
            snackbar.visibility = View.GONE
        }
        render(allItems)
    }

    override fun onStart() {
        super.onStart()
        loadSnapshots()
    }

    private fun loadSnapshots() {
        if (cameraId <= 0L) return
        lifecycleScope.launch {
            try {
                val end = System.currentTimeMillis()
                val start = end - 7 * 24 * 60 * 60 * 1000L
                val timeline = withContext(Dispatchers.IO) {
                    repository.getTimeline(cameraId, start, end)
                }
                val items = timeline.thumbnails.map { mapThumbnail(it) }.sortedByDescending { it.tsMs }
                allItems.clear()
                allItems.addAll(items)
                findViewById<TextView>(R.id.snapshot_camera_info).text = "$cameraName · ${allItems.size} photos"
                render(allItems)
            } catch (ex: Exception) {
                if (ex is HttpException && ex.code() == 401) {
                    finish()
                }
            }
        }
    }

    private fun toggleSelection(item: SnapshotItem) {
        if (selectedIds.contains(item.id)) {
            selectedIds.remove(item.id)
        } else {
            selectedIds.add(item.id)
        }
        refreshSelectionUi()
        renderCurrent()
    }

    private fun refreshSelectionUi() {
        val info = findViewById<TextView>(R.id.snapshot_camera_info)
        info.text = if (selecting) {
            "${selectedIds.size} selected"
        } else {
            "$cameraName · ${allItems.size} photos"
        }
        findViewById<View>(R.id.snapshot_delete_toggle).alpha = if (selectedIds.isEmpty()) 0.45f else 1f
    }

    private fun renderCurrent() {
        render(allItems)
    }

    private fun showSnackbar(message: String) {
        snackbarText.text = message
        snackbar.visibility = View.VISIBLE
    }

    private fun mapThumbnail(thumbnail: HistoryThumbnailDto): SnapshotItem {
        val ts = thumbnail.ts
        val timeLabel = SimpleDateFormat("HH:mm", Locale.getDefault()).format(Date(ts))
        val todayStart = SimpleDateFormat("yyyyMMdd", Locale.US).format(Date())
        val itemDay = SimpleDateFormat("yyyyMMdd", Locale.US).format(Date(ts))
        val group = if (itemDay == todayStart) Group.TODAY else Group.WEEK
        return SnapshotItem(
            id = "thumb-$ts",
            tsMs = ts,
            timeLabel = timeLabel,
            url = thumbnail.url,
            group = group,
        )
    }

    private fun openPlayback(item: SnapshotItem) {
        if (cameraId <= 0L) return
        startActivity(
            android.content.Intent(this, com.reallive.android.ui.history.HistoryActivity::class.java).apply {
                putExtra(com.reallive.android.ui.watch.WatchActivity.EXTRA_CAMERA_ID, cameraId)
                putExtra(com.reallive.android.ui.watch.WatchActivity.EXTRA_CAMERA_NAME, cameraName)
                putExtra(EXTRA_START_TS, item.tsMs)
            },
        )
    }

    private fun render(items: List<SnapshotItem>) {
        val today = items.filter { it.group == Group.TODAY }
        val week = items.filter { it.group == Group.WEEK }
        todayAdapter.submitList(today)
        yesterdayAdapter.submitList(week)
        todayLabel.visibility = if (today.isEmpty()) View.GONE else View.VISIBLE
        yesterdayLabel.visibility = if (week.isEmpty()) View.GONE else View.VISIBLE
    }

    companion object {
        const val EXTRA_CAMERA_ID = "extra_camera_id"
        const val EXTRA_CAMERA_NAME = "extra_camera_name"
        const val EXTRA_START_TS = "extra_start_ts"
    }
}

private enum class Group {
    TODAY,
    WEEK,
}

private data class SnapshotItem(
    val id: String,
    val tsMs: Long,
    val timeLabel: String,
    val url: String,
    val group: Group,
)

private class SnapshotAdapter(
    private val onClick: (SnapshotItem) -> Unit,
    private val isSelecting: () -> Boolean,
    private val isSelected: (String) -> Boolean,
) : ListAdapter<SnapshotItem, SnapshotAdapter.SnapshotViewHolder>(DiffCallback()) {
    override fun onCreateViewHolder(parent: ViewGroup, viewType: Int): SnapshotViewHolder {
        val view = LayoutInflater.from(parent.context).inflate(R.layout.item_snapshot, parent, false)
        return SnapshotViewHolder(view, onClick, isSelecting, isSelected)
    }

    override fun onBindViewHolder(holder: SnapshotViewHolder, position: Int) {
        holder.bind(getItem(position))
    }

    class SnapshotViewHolder(
        itemView: View,
        private val onClick: (SnapshotItem) -> Unit,
        private val isSelecting: () -> Boolean,
        private val isSelected: (String) -> Boolean,
    ) : RecyclerView.ViewHolder(itemView) {
        private val imageView: ImageView = itemView.findViewById(R.id.snapshot_image)
        private val timeText: TextView = itemView.findViewById(R.id.snapshot_time)
        private val selectedOverlay: View = itemView.findViewById(R.id.snapshot_selected_overlay)
        private var item: SnapshotItem? = null

        init {
            itemView.setOnClickListener { item?.let(onClick) }
        }

        fun bind(data: SnapshotItem) {
            item = data
            timeText.text = data.timeLabel
            imageView.setBackgroundColor(itemView.context.getColor(R.color.rl_surface_alt))
            imageView.scaleType = ImageView.ScaleType.CENTER_CROP
            imageView.load(data.url) {
                crossfade(true)
            }
            selectedOverlay.visibility = if (isSelecting() && isSelected(data.id)) View.VISIBLE else View.GONE
        }
    }

    private class DiffCallback : DiffUtil.ItemCallback<SnapshotItem>() {
        override fun areItemsTheSame(oldItem: SnapshotItem, newItem: SnapshotItem): Boolean {
            return oldItem.id == newItem.id
        }

        override fun areContentsTheSame(oldItem: SnapshotItem, newItem: SnapshotItem): Boolean {
            return oldItem == newItem
        }
    }
}
