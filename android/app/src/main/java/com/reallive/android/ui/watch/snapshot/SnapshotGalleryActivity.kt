package com.reallive.android.ui.watch.snapshot

import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.ImageButton
import android.widget.ImageView
import android.widget.TextView
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import androidx.recyclerview.widget.DiffUtil
import androidx.recyclerview.widget.GridLayoutManager
import androidx.recyclerview.widget.ListAdapter
import androidx.recyclerview.widget.RecyclerView
import com.google.android.material.appbar.MaterialToolbar
import com.google.android.material.chip.ChipGroup
import com.reallive.android.R

class SnapshotGalleryActivity : AppCompatActivity() {
    private lateinit var adapter: SnapshotAdapter
    private lateinit var statusText: TextView
    private lateinit var emptyText: TextView
    private var cameraName: String = "Camera"

    private val allItems = listOf(
        SnapshotItem("1", "Today 09:35", R.drawable.ic_tab_monitor_24, Group.TODAY),
        SnapshotItem("2", "Today 08:52", android.R.drawable.ic_menu_myplaces, Group.TODAY),
        SnapshotItem("3", "Today 07:20", android.R.drawable.ic_media_play, Group.TODAY),
        SnapshotItem("4", "Today 06:15", R.drawable.ic_tab_monitor_24, Group.TODAY),
        SnapshotItem("5", "Yesterday 22:18", android.R.drawable.ic_media_play, Group.WEEK),
        SnapshotItem("6", "Yesterday 19:05", R.drawable.ic_tab_monitor_24, Group.WEEK),
        SnapshotItem("7", "Yesterday 17:30", android.R.drawable.ic_menu_myplaces, Group.WEEK),
        SnapshotItem("8", "2 days ago 11:08", R.drawable.ic_tab_monitor_24, Group.WEEK),
    )

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_snapshot_gallery)

        cameraName = intent.getStringExtra(EXTRA_CAMERA_NAME) ?: "Front Door"

        findViewById<MaterialToolbar>(R.id.snapshot_toolbar).setNavigationOnClickListener { finish() }
        findViewById<TextView>(R.id.snapshot_camera_info).text = "$cameraName · ${allItems.size} photos"

        statusText = findViewById(R.id.snapshot_status)
        emptyText = findViewById(R.id.snapshot_empty)
        adapter = SnapshotAdapter(
            onClick = { item ->
                Toast.makeText(this, "Open ${item.timeLabel} (mock)", Toast.LENGTH_SHORT).show()
            },
            onShare = { item ->
                Toast.makeText(this, "Share ${item.timeLabel} (mock)", Toast.LENGTH_SHORT).show()
            },
        )

        findViewById<RecyclerView>(R.id.snapshot_recycler).apply {
            layoutManager = GridLayoutManager(this@SnapshotGalleryActivity, 2)
            adapter = this@SnapshotGalleryActivity.adapter
        }

        val filterGroup = findViewById<ChipGroup>(R.id.snapshot_filter_group)
        filterGroup.check(R.id.snapshot_filter_all)
        filterGroup.setOnCheckedStateChangeListener { _, checkedIds ->
            val checked = checkedIds.firstOrNull() ?: R.id.snapshot_filter_all
            val filtered = when (checked) {
                R.id.snapshot_filter_today -> allItems.filter { it.group == Group.TODAY }
                R.id.snapshot_filter_week -> allItems.filter { it.group == Group.WEEK || it.group == Group.TODAY }
                else -> allItems
            }
            render(filtered)
        }
        render(allItems)
    }

    private fun render(items: List<SnapshotItem>) {
        adapter.submitList(items)
        emptyText.visibility = if (items.isEmpty()) View.VISIBLE else View.GONE
        statusText.text = "Showing ${items.size} snapshots"
    }

    companion object {
        const val EXTRA_CAMERA_ID = "extra_camera_id"
        const val EXTRA_CAMERA_NAME = "extra_camera_name"
    }
}

private enum class Group {
    TODAY,
    WEEK,
}

private data class SnapshotItem(
    val id: String,
    val timeLabel: String,
    val iconRes: Int,
    val group: Group,
)

private class SnapshotAdapter(
    private val onClick: (SnapshotItem) -> Unit,
    private val onShare: (SnapshotItem) -> Unit,
) : ListAdapter<SnapshotItem, SnapshotAdapter.SnapshotViewHolder>(DiffCallback()) {
    override fun onCreateViewHolder(parent: ViewGroup, viewType: Int): SnapshotViewHolder {
        val view = LayoutInflater.from(parent.context).inflate(R.layout.item_snapshot, parent, false)
        return SnapshotViewHolder(view, onClick, onShare)
    }

    override fun onBindViewHolder(holder: SnapshotViewHolder, position: Int) {
        holder.bind(getItem(position))
    }

    class SnapshotViewHolder(
        itemView: View,
        private val onClick: (SnapshotItem) -> Unit,
        private val onShare: (SnapshotItem) -> Unit,
    ) : RecyclerView.ViewHolder(itemView) {
        private val imageView: ImageView = itemView.findViewById(R.id.snapshot_image)
        private val timeText: TextView = itemView.findViewById(R.id.snapshot_time)
        private val shareButton: ImageButton = itemView.findViewById(R.id.snapshot_share)
        private var item: SnapshotItem? = null

        init {
            itemView.setOnClickListener { item?.let(onClick) }
            shareButton.setOnClickListener { item?.let(onShare) }
        }

        fun bind(data: SnapshotItem) {
            item = data
            timeText.text = data.timeLabel
            imageView.setImageResource(data.iconRes)
            imageView.setBackgroundColor(itemView.context.getColor(R.color.rl_surface_alt))
            imageView.scaleType = ImageView.ScaleType.CENTER
            imageView.setPadding(0, 36, 0, 36)
            imageView.imageTintList = itemView.context.getColorStateList(R.color.rl_text_secondary)
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
