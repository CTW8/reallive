package com.reallive.android.ui.dashboard

import android.graphics.drawable.GradientDrawable
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.ImageView
import android.widget.TextView
import androidx.core.content.ContextCompat
import androidx.recyclerview.widget.DiffUtil
import androidx.recyclerview.widget.ListAdapter
import androidx.recyclerview.widget.RecyclerView
import coil.load
import com.reallive.android.R
import com.reallive.android.network.CameraDto
import java.util.Locale

class CameraAdapter(
    private val baseUrl: String,
    private val onClick: (CameraDto) -> Unit,
    private val onLongClick: (CameraDto) -> Unit,
    private val onMenuClick: (CameraDto) -> Unit,
) : ListAdapter<CameraDto, CameraAdapter.CameraViewHolder>(DiffCallback()) {

    override fun onCreateViewHolder(parent: ViewGroup, viewType: Int): CameraViewHolder {
        val v = LayoutInflater.from(parent.context)
            .inflate(R.layout.item_camera_card, parent, false)
        return CameraViewHolder(v)
    }

    override fun onBindViewHolder(holder: CameraViewHolder, position: Int) {
        val item = getItem(position)
        holder.bind(item, baseUrl)
        holder.itemView.setOnClickListener { onClick(item) }
        holder.itemView.setOnLongClickListener {
            onLongClick(item)
            true
        }
        holder.bindMenuClick { onMenuClick(item) }
    }

    class CameraViewHolder(view: View) : RecyclerView.ViewHolder(view) {
        private val nameText: TextView = view.findViewById(R.id.camera_name)
        private val statusText: TextView = view.findViewById(R.id.camera_status)
        private val streamKeyText: TextView = view.findViewById(R.id.camera_stream_key)
        private val runtimeText: TextView = view.findViewById(R.id.camera_runtime)
        private val statusDot: View = view.findViewById(R.id.status_dot)
        private val thumbnailView: ImageView = view.findViewById(R.id.camera_thumbnail)
        private val thumbnailHint: TextView = view.findViewById(R.id.camera_thumbnail_hint)
        private val moreButton: View = view.findViewById(R.id.camera_more)

        fun bindMenuClick(onClick: () -> Unit) {
            moreButton.setOnClickListener { onClick() }
        }

        fun bind(item: CameraDto, baseUrl: String) {
            nameText.text = item.name

            val status = (item.status ?: "offline").lowercase(Locale.US)
            val (dotColor, labelColor, statusLabel) = when (status) {
                "online" -> Triple(
                    ContextCompat.getColor(itemView.context, R.color.rl_brand),
                    ContextCompat.getColor(itemView.context, R.color.rl_brand),
                    "Online",
                )
                "streaming" -> Triple(
                    ContextCompat.getColor(itemView.context, R.color.rl_warning),
                    ContextCompat.getColor(itemView.context, R.color.rl_warning),
                    "REC",
                )
                else -> Triple(
                    ContextCompat.getColor(itemView.context, R.color.rl_offline),
                    ContextCompat.getColor(itemView.context, R.color.rl_text_muted),
                    "Offline",
                )
            }

            statusDot.background = GradientDrawable().apply {
                shape = GradientDrawable.OVAL
                setColor(dotColor)
            }
            statusText.text = statusLabel
            statusText.setTextColor(labelColor)

            val resText = item.resolution ?: "auto"
            val location = when (item.name.lowercase(Locale.US)) {
                "front door" -> "Main Entrance"
                "garage" -> "Side Building"
                "living room" -> "Indoor"
                "backyard" -> "Garden"
                "warehouse" -> "Storage"
                else -> "Camera Area"
            }
            streamKeyText.text = "$location · $resText"

            val updatedAt = item.device?.updatedAt
            runtimeText.text = if (updatedAt != null && updatedAt > 0) "Updated ${formatSince(updatedAt)}" else ""

            val thumbUrl = absoluteUrl(baseUrl, item.thumbnailUrl)
            if (thumbUrl == null) {
                thumbnailView.setImageDrawable(null)
                thumbnailHint.visibility = View.VISIBLE
            } else {
                thumbnailHint.visibility = View.GONE
                thumbnailView.load(thumbUrl) {
                    crossfade(true)
                }
            }
        }

        private fun absoluteUrl(baseUrl: String, path: String?): String? {
            if (path.isNullOrBlank()) return null
            if (path.startsWith("http://") || path.startsWith("https://")) return path
            val root = if (baseUrl.endsWith('/')) baseUrl.dropLast(1) else baseUrl
            val rel = if (path.startsWith('/')) path else "/$path"
            return "$root$rel"
        }

        private fun formatSince(tsMs: Long): String {
            val delta = (System.currentTimeMillis() - tsMs).coerceAtLeast(0L)
            val sec = delta / 1000
            if (sec < 60) return "${sec}s ago"
            val min = sec / 60
            if (min < 60) return "${min}m ago"
            val h = min / 60
            return "${h}h ago"
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
