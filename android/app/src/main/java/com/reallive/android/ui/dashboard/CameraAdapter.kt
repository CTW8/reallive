package com.reallive.android.ui.dashboard

import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.ImageView
import android.widget.TextView
import coil.load
import androidx.recyclerview.widget.RecyclerView
import com.reallive.android.R
import com.reallive.android.network.CameraDto

class CameraAdapter(
    private val baseUrl: String,
    private val onClick: (CameraDto) -> Unit,
) : RecyclerView.Adapter<CameraAdapter.CameraViewHolder>() {

    private val items = mutableListOf<CameraDto>()

    fun submitList(newItems: List<CameraDto>) {
        items.clear()
        items.addAll(newItems)
        notifyDataSetChanged()
    }

    override fun onCreateViewHolder(parent: ViewGroup, viewType: Int): CameraViewHolder {
        val v = LayoutInflater.from(parent.context)
            .inflate(R.layout.item_camera_card, parent, false)
        return CameraViewHolder(v)
    }

    override fun getItemCount(): Int = items.size

    override fun onBindViewHolder(holder: CameraViewHolder, position: Int) {
        val item = items[position]
        holder.bind(item, baseUrl)
        holder.itemView.setOnClickListener { onClick(item) }
    }

    class CameraViewHolder(view: View) : RecyclerView.ViewHolder(view) {
        private val nameText: TextView = view.findViewById(R.id.camera_name)
        private val statusText: TextView = view.findViewById(R.id.camera_status)
        private val streamKeyText: TextView = view.findViewById(R.id.camera_stream_key)
        private val runtimeText: TextView = view.findViewById(R.id.camera_runtime)
        private val statusDot: View = view.findViewById(R.id.status_dot)
        private val thumbnailView: ImageView = view.findViewById(R.id.camera_thumbnail)
        private val thumbnailHint: TextView = view.findViewById(R.id.camera_thumbnail_hint)

        fun bind(item: CameraDto, baseUrl: String) {
            nameText.text = item.name
            streamKeyText.text = item.stream_key
            val status = item.status ?: "offline"
            statusText.text = status
            val color = when (status.lowercase()) {
                "streaming" -> 0xFF4CAF50.toInt()
                "online" -> 0xFF00BCD4.toInt()
                else -> 0xFFF44336.toInt()
            }
            statusDot.setBackgroundColor(color)

            val updatedAt = item.device?.updatedAt
            runtimeText.text = if (updatedAt != null && updatedAt > 0) {
                "heartbeat: ${formatSince(updatedAt)}"
            } else {
                "heartbeat: -"
            }

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
}
