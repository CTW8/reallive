package com.reallive.android.ui.camera

import android.content.Intent
import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import androidx.core.content.ContextCompat
import androidx.recyclerview.widget.DiffUtil
import androidx.recyclerview.widget.LinearLayoutManager
import androidx.recyclerview.widget.ListAdapter
import androidx.recyclerview.widget.RecyclerView
import com.google.android.material.appbar.MaterialToolbar
import com.google.android.material.button.MaterialButtonToggleGroup
import com.reallive.android.R
import com.reallive.android.config.AppConfig
import com.reallive.android.network.CameraDto
import com.reallive.android.ui.watch.WatchActivity
import java.util.Locale

class CameraListActivity : AppCompatActivity() {
    private lateinit var appConfig: AppConfig
    private lateinit var statusText: TextView
    private lateinit var adapter: CameraListAdapter

    private val cameras = mutableListOf<CameraDto>()
    private var currentFilter = Filter.ALL

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        appConfig = AppConfig(this)
        if (appConfig.getToken().isNullOrBlank()) {
            finish()
            return
        }
        setContentView(R.layout.activity_camera_list)

        findViewById<MaterialToolbar>(R.id.camera_list_toolbar).setNavigationOnClickListener { finish() }
        statusText = findViewById(R.id.camera_list_status)

        adapter = CameraListAdapter(
            onClick = { openWatch(it) },
            onLongClick = { openCameraSettings(it) },
        )
        val recycler = findViewById<RecyclerView>(R.id.camera_list_recycler)
        recycler.layoutManager = LinearLayoutManager(this)
        recycler.adapter = adapter

        val filters = findViewById<MaterialButtonToggleGroup>(R.id.camera_list_filters)
        filters.check(R.id.camera_filter_all)
        filters.addOnButtonCheckedListener { _, checkedId, isChecked ->
            if (!isChecked) return@addOnButtonCheckedListener
            currentFilter = when (checkedId) {
                R.id.camera_filter_online -> Filter.ONLINE
                R.id.camera_filter_offline -> Filter.OFFLINE
                R.id.camera_filter_recording -> Filter.RECORDING
                else -> Filter.ALL
            }
            applyFilter()
        }
    }

    override fun onStart() {
        super.onStart()
        cameras.clear()
        cameras.addAll(
            listOf(
                CameraDto(1, "Front Door", "front-door", resolution = "1080p", status = "streaming"),
                CameraDto(2, "Garage", "garage", resolution = "1080p", status = "online"),
                CameraDto(3, "Living Room", "living-room", resolution = "2K", status = "online"),
                CameraDto(4, "Backyard", "backyard", resolution = "1080p", status = "offline"),
                CameraDto(5, "Warehouse", "warehouse", resolution = "4K", status = "online"),
                CameraDto(6, "Bedroom", "bedroom", resolution = "1080p", status = "online"),
                CameraDto(7, "Office", "office", resolution = "2K", status = "offline"),
                CameraDto(8, "Side Gate", "side-gate", resolution = "1080p", status = "online"),
            ),
        )
        applyFilter()
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
        statusText.text = "${filtered.size}/${cameras.size} cameras"
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

    private enum class Filter {
        ALL,
        ONLINE,
        OFFLINE,
        RECORDING,
    }
}

private class CameraListAdapter(
    private val onClick: (CameraDto) -> Unit,
    private val onLongClick: (CameraDto) -> Unit,
) : ListAdapter<CameraDto, CameraListAdapter.CameraViewHolder>(DiffCallback()) {
    override fun onCreateViewHolder(parent: ViewGroup, viewType: Int): CameraViewHolder {
        val view = LayoutInflater.from(parent.context)
            .inflate(R.layout.item_camera_list_row, parent, false)
        return CameraViewHolder(view, onClick, onLongClick)
    }

    override fun onBindViewHolder(holder: CameraViewHolder, position: Int) {
        holder.bind(getItem(position))
    }

    class CameraViewHolder(
        itemView: View,
        private val onClick: (CameraDto) -> Unit,
        private val onLongClick: (CameraDto) -> Unit,
    ) : RecyclerView.ViewHolder(itemView) {
        private val nameText: TextView = itemView.findViewById(R.id.camera_row_name)
        private val metaText: TextView = itemView.findViewById(R.id.camera_row_meta)
        private val badgeText: TextView = itemView.findViewById(R.id.camera_row_badge)
        private var camera: CameraDto? = null

        init {
            itemView.setOnClickListener { camera?.let(onClick) }
            itemView.setOnLongClickListener {
                camera?.let(onLongClick)
                true
            }
        }

        fun bind(item: CameraDto) {
            camera = item
            nameText.text = item.name
            metaText.text = "${item.resolution ?: "auto"} • ${item.stream_key.take(10)}"
            val status = item.status ?: "offline"
            badgeText.text = status
            val statusLower = status.lowercase(Locale.US)
            if (statusLower == "streaming") {
                badgeText.setTextColor(ContextCompat.getColor(itemView.context, R.color.rl_success))
                badgeText.setBackgroundColor(ContextCompat.getColor(itemView.context, R.color.rl_status_streaming_bg))
            } else if (statusLower == "online") {
                badgeText.setTextColor(ContextCompat.getColor(itemView.context, R.color.rl_secondary))
                badgeText.setBackgroundColor(ContextCompat.getColor(itemView.context, R.color.rl_status_online_bg))
            } else {
                badgeText.setTextColor(ContextCompat.getColor(itemView.context, R.color.rl_text_secondary))
                badgeText.setBackgroundColor(ContextCompat.getColor(itemView.context, R.color.rl_status_offline_bg))
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
