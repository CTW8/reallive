package com.reallive.android.ui.watch.snapshot

import android.content.ContentUris
import android.content.Intent
import android.net.Uri
import android.os.Bundle
import android.provider.MediaStore
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.ImageView
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import androidx.core.content.ContextCompat
import androidx.lifecycle.lifecycleScope
import androidx.recyclerview.widget.DiffUtil
import androidx.recyclerview.widget.GridLayoutManager
import androidx.recyclerview.widget.ListAdapter
import androidx.recyclerview.widget.RecyclerView
import coil.load
import com.reallive.android.R
import com.reallive.android.config.AppConfig
import com.reallive.android.data.CameraRepository
import com.reallive.android.network.ApiFactory
import com.reallive.android.network.HistoryThumbnailDto
import com.reallive.android.ui.auth.AuthGuard
import com.reallive.android.ui.auth.LoginActivity
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import retrofit2.HttpException
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

class SnapshotGalleryActivity : AppCompatActivity() {
    private lateinit var appConfig: AppConfig
    private lateinit var pageTitle: TextView
    private lateinit var infoText: TextView
    private lateinit var todayLabel: TextView
    private lateinit var olderLabel: TextView
    private lateinit var todayAdapter: SnapshotAdapter
    private lateinit var olderAdapter: SnapshotAdapter
    private lateinit var repository: CameraRepository
    private val allItems = mutableListOf<SnapshotItem>()
    private val selectedIds = linkedSetOf<String>()
    private var selecting: Boolean = false
    private lateinit var snackbar: View
    private lateinit var snackbarText: TextView
    private var cameraName: String = "Camera"
    private var cameraId: Long = -1L
    private var currentFilter: Filter = Filter.ALL
    private var isZh: Boolean = false

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_snapshot_gallery)

        appConfig = AppConfig(this)
        if (appConfig.getToken().isNullOrBlank()) {
            redirectToLogin()
            return
        }
        isZh = appConfig.getAppLanguage().startsWith("zh", true)
        repository = CameraRepository(ApiFactory.createAuthorized(appConfig))
        cameraName = intent.getStringExtra(EXTRA_CAMERA_NAME) ?: if (isZh) "摄像头" else "Camera"
        cameraId = intent.getLongExtra(EXTRA_CAMERA_ID, -1L)

        pageTitle = findViewById(R.id.snapshot_page_title)
        infoText = findViewById(R.id.snapshot_camera_info)
        todayLabel = findViewById(R.id.snapshot_today_label)
        olderLabel = findViewById(R.id.snapshot_yesterday_label)
        snackbar = findViewById(R.id.snapshot_snackbar)
        snackbarText = findViewById(R.id.snapshot_snackbar_message)

        pageTitle.text = if (isZh) "截图相册" else "Snapshot Gallery"
        findViewById<View>(R.id.snapshot_back).setOnClickListener { finish() }

        todayAdapter = SnapshotAdapter(
            onClick = { item ->
                if (selecting) toggleSelection(item) else openItem(item)
            },
            isSelecting = { selecting },
            isSelected = { id -> selectedIds.contains(id) },
        )
        olderAdapter = SnapshotAdapter(
            onClick = { item ->
                if (selecting) toggleSelection(item) else openItem(item)
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
            adapter = olderAdapter
        }

        val filterAll = findViewById<TextView>(R.id.snapshot_filter_all)
        val filterToday = findViewById<TextView>(R.id.snapshot_filter_today)
        val filterWeek = findViewById<TextView>(R.id.snapshot_filter_week)
        val filterMonth = findViewById<TextView>(R.id.snapshot_filter_month)
        if (isZh) {
            filterAll.text = "全部"
            filterToday.text = "今天"
            filterWeek.text = "本周"
            filterMonth.text = "本月"
            olderLabel.text = "更早"
            findViewById<TextView>(R.id.snapshot_snackbar_action).text = "关闭"
        }
        filterAll.setOnClickListener { setFilter(Filter.ALL) }
        filterToday.setOnClickListener { setFilter(Filter.TODAY) }
        filterWeek.setOnClickListener { setFilter(Filter.WEEK) }
        filterMonth.setOnClickListener { setFilter(Filter.MONTH) }

        findViewById<View>(R.id.snapshot_select_toggle).setOnClickListener {
            selecting = !selecting
            if (!selecting) selectedIds.clear()
            refreshSelectionUi()
            renderCurrent()
        }
        findViewById<View>(R.id.snapshot_delete_toggle).setOnClickListener {
            if (selectedIds.isEmpty()) return@setOnClickListener
            deleteSelected()
        }
        findViewById<View>(R.id.snapshot_snackbar_action).setOnClickListener {
            snackbar.visibility = View.GONE
        }
        setFilter(Filter.ALL)
        refreshSelectionUi()
    }

    override fun onStart() {
        super.onStart()
        if (appConfig.shouldRequireReauth()) {
            redirectToLogin(forceReauth = true)
            return
        }
        appConfig.markAuthenticated()
        loadSnapshots()
    }

    private fun loadSnapshots() {
        lifecycleScope.launch {
            val local = withContext(Dispatchers.IO) { queryLocalSnapshots() }
            val merged = mutableListOf<SnapshotItem>()
            merged.addAll(local)
            if (cameraId > 0L) {
                val remote = runCatching { withContext(Dispatchers.IO) { queryRemoteSnapshots() } }.getOrDefault(emptyList())
                remote.forEach { r ->
                    if (merged.none { it.tsMs == r.tsMs && it.remoteUrl == r.remoteUrl }) {
                        merged.add(r)
                    }
                }
            }
            merged.sortByDescending { it.tsMs }
            allItems.clear()
            allItems.addAll(merged)
            refreshSelectionUi()
            renderCurrent()
        }
    }

    private suspend fun queryRemoteSnapshots(): List<SnapshotItem> {
        val end = System.currentTimeMillis()
        val start = end - 30L * 24L * 60L * 60L * 1000L
        return try {
            val timeline = repository.getTimeline(cameraId, start, end)
            timeline.thumbnails.map { mapRemoteThumbnail(it) }
        } catch (ex: Exception) {
            if (ex is HttpException && ex.code() == 401) {
                handleUnauthorized()
            }
            emptyList()
        }
    }

    private suspend fun handleUnauthorized() {
        val valid = AuthGuard.isSessionValid(appConfig)
        if (!valid) {
            appConfig.clearAuth()
            withContext(Dispatchers.Main) {
                redirectToLogin()
            }
        }
    }

    private fun redirectToLogin(forceReauth: Boolean = false) {
        startActivity(
            Intent(this, LoginActivity::class.java).apply {
                if (forceReauth) putExtra(LoginActivity.EXTRA_FORCE_REAUTH, true)
            },
        )
        finish()
    }

    private fun queryLocalSnapshots(): List<SnapshotItem> {
        val items = mutableListOf<SnapshotItem>()
        val projection = arrayOf(
            MediaStore.Images.Media._ID,
            MediaStore.Images.Media.DATE_TAKEN,
            MediaStore.Images.Media.DATE_ADDED,
            MediaStore.Images.Media.DISPLAY_NAME,
        )
        val selection = "${MediaStore.Images.Media.DISPLAY_NAME} LIKE ?"
        val args = arrayOf("reallive_%")
        val sort = "${MediaStore.Images.Media.DATE_ADDED} DESC"
        contentResolver.query(
            MediaStore.Images.Media.EXTERNAL_CONTENT_URI,
            projection,
            selection,
            args,
            sort,
        )?.use { c ->
            val idIdx = c.getColumnIndexOrThrow(MediaStore.Images.Media._ID)
            val takenIdx = c.getColumnIndexOrThrow(MediaStore.Images.Media.DATE_TAKEN)
            val addedIdx = c.getColumnIndexOrThrow(MediaStore.Images.Media.DATE_ADDED)
            while (c.moveToNext()) {
                val id = c.getLong(idIdx)
                val taken = c.getLong(takenIdx)
                val added = c.getLong(addedIdx)
                val ts = if (taken > 0L) taken else added * 1000L
                val uri = ContentUris.withAppendedId(MediaStore.Images.Media.EXTERNAL_CONTENT_URI, id)
                items.add(
                    SnapshotItem(
                        id = "local-$id",
                        tsMs = ts,
                        timeLabel = formatTime(ts),
                        localUri = uri.toString(),
                        remoteUrl = null,
                        group = resolveGroup(ts),
                    ),
                )
            }
        }
        return items
    }

    private fun mapRemoteThumbnail(thumbnail: HistoryThumbnailDto): SnapshotItem {
        val ts = thumbnail.ts
        return SnapshotItem(
            id = "remote-${ts}-${thumbnail.url.hashCode()}",
            tsMs = ts,
            timeLabel = formatTime(ts),
            localUri = null,
            remoteUrl = thumbnail.url,
            group = resolveGroup(ts),
        )
    }

    private fun toggleSelection(item: SnapshotItem) {
        if (selectedIds.contains(item.id)) selectedIds.remove(item.id) else selectedIds.add(item.id)
        refreshSelectionUi()
        renderCurrent()
    }

    private fun deleteSelected() {
        val toDelete = allItems.filter { selectedIds.contains(it.id) }
        var removed = 0
        toDelete.forEach { item ->
            if (!item.localUri.isNullOrBlank()) {
                val uri = Uri.parse(item.localUri)
                val r = contentResolver.delete(uri, null, null)
                if (r > 0) removed += 1
            } else {
                removed += 1
            }
        }
        allItems.removeAll { selectedIds.contains(it.id) }
        selectedIds.clear()
        showSnackbar(
            if (isZh) "已删除 $removed 张截图" else "$removed snapshots removed",
        )
        refreshSelectionUi()
        renderCurrent()
    }

    private fun openItem(item: SnapshotItem) {
        if (!item.localUri.isNullOrBlank()) {
            val uri = Uri.parse(item.localUri)
            val intent = Intent(Intent.ACTION_VIEW).apply {
                setDataAndType(uri, "image/*")
                addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
            }
            startActivity(intent)
            return
        }
        if (cameraId <= 0L) return
        startActivity(
            Intent(this, com.reallive.android.ui.history.HistoryActivity::class.java).apply {
                putExtra(com.reallive.android.ui.watch.WatchActivity.EXTRA_CAMERA_ID, cameraId)
                putExtra(com.reallive.android.ui.watch.WatchActivity.EXTRA_CAMERA_NAME, cameraName)
                putExtra(EXTRA_START_TS, item.tsMs)
            },
        )
    }

    private fun refreshSelectionUi() {
        infoText.text = if (selecting) {
            if (isZh) "已选择 ${selectedIds.size}" else "${selectedIds.size} selected"
        } else {
            if (isZh) "$cameraName · ${allItems.size} 张" else "$cameraName · ${allItems.size} photos"
        }
        findViewById<View>(R.id.snapshot_delete_toggle).alpha = if (selectedIds.isEmpty()) 0.45f else 1f
    }

    private fun setFilter(filter: Filter) {
        currentFilter = filter
        updateFilterUi()
        renderCurrent()
    }

    private fun updateFilterUi() {
        val all = findViewById<TextView>(R.id.snapshot_filter_all)
        val today = findViewById<TextView>(R.id.snapshot_filter_today)
        val week = findViewById<TextView>(R.id.snapshot_filter_week)
        val month = findViewById<TextView>(R.id.snapshot_filter_month)
        applyFilterStyle(all, currentFilter == Filter.ALL)
        applyFilterStyle(today, currentFilter == Filter.TODAY)
        applyFilterStyle(week, currentFilter == Filter.WEEK)
        applyFilterStyle(month, currentFilter == Filter.MONTH)
    }

    private fun applyFilterStyle(view: TextView, selected: Boolean) {
        view.setBackgroundResource(if (selected) R.drawable.bg_btn_tonal else R.drawable.bg_btn_outline)
        val color = if (selected) {
            0xFFE5DFF9.toInt()
        } else {
            ContextCompat.getColor(this, R.color.auth_on_surface_variant)
        }
        view.setTextColor(color)
    }

    private fun renderCurrent() {
        val now = System.currentTimeMillis()
        val filtered = allItems.filter { item ->
            when (currentFilter) {
                Filter.ALL -> true
                Filter.TODAY -> item.group == Group.TODAY
                Filter.WEEK -> now - item.tsMs <= 7L * 24L * 60L * 60L * 1000L
                Filter.MONTH -> now - item.tsMs <= 30L * 24L * 60L * 60L * 1000L
            }
        }
        val today = filtered.filter { it.group == Group.TODAY }
        val older = filtered.filter { it.group != Group.TODAY }
        todayAdapter.submitList(today)
        olderAdapter.submitList(older)
        todayLabel.visibility = if (today.isEmpty()) View.GONE else View.VISIBLE
        olderLabel.visibility = if (older.isEmpty()) View.GONE else View.VISIBLE
    }

    private fun showSnackbar(message: String) {
        snackbarText.text = message
        snackbar.visibility = View.VISIBLE
    }

    private fun formatTime(ts: Long): String {
        return SimpleDateFormat("HH:mm", Locale.getDefault()).format(Date(ts))
    }

    private fun resolveGroup(ts: Long): Group {
        val today = SimpleDateFormat("yyyyMMdd", Locale.US).format(Date())
        val itemDay = SimpleDateFormat("yyyyMMdd", Locale.US).format(Date(ts))
        return if (today == itemDay) Group.TODAY else Group.OLDER
    }

    companion object {
        const val EXTRA_CAMERA_ID = "extra_camera_id"
        const val EXTRA_CAMERA_NAME = "extra_camera_name"
        const val EXTRA_STREAM_KEY = "extra_stream_key"
        const val EXTRA_START_TS = "extra_start_ts"
    }
}

private enum class Group {
    TODAY,
    OLDER,
}

private enum class Filter {
    ALL,
    TODAY,
    WEEK,
    MONTH,
}

private data class SnapshotItem(
    val id: String,
    val tsMs: Long,
    val timeLabel: String,
    val localUri: String?,
    val remoteUrl: String?,
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
            val src = data.localUri ?: data.remoteUrl
            imageView.load(src) { crossfade(true) }
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
