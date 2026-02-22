package com.reallive.android.ui.watch

import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.ImageView
import android.widget.TextView
import androidx.recyclerview.widget.RecyclerView
import coil.load
import com.reallive.android.R
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

data class TimelineEventItem(
    val tsMs: Long,
    val type: String,
    val score: Double,
    val thumbnailUrl: String? = null,
)

class TimelineEventAdapter(
    private val onClick: (TimelineEventItem) -> Unit,
) : RecyclerView.Adapter<TimelineEventAdapter.EventViewHolder>() {

    private val items = mutableListOf<TimelineEventItem>()
    private val dateFormat = SimpleDateFormat("HH:mm", Locale.getDefault())

    fun submitList(data: List<TimelineEventItem>) {
        items.clear()
        items.addAll(data)
        notifyDataSetChanged()
    }

    override fun onCreateViewHolder(parent: ViewGroup, viewType: Int): EventViewHolder {
        val view = LayoutInflater.from(parent.context)
            .inflate(R.layout.item_timeline_event, parent, false)
        return EventViewHolder(view)
    }

    override fun getItemCount(): Int = items.size

    override fun onBindViewHolder(holder: EventViewHolder, position: Int) {
        val item = items[position]
        holder.bind(item, dateFormat)
        holder.itemView.setOnClickListener { onClick(item) }
    }

    class EventViewHolder(view: View) : RecyclerView.ViewHolder(view) {
        private val dotView: View = view.findViewById(R.id.event_dot)
        private val titleView: TextView = view.findViewById(R.id.event_title)
        private val timeView: TextView = view.findViewById(R.id.event_time)
        private val subtitleView: TextView = view.findViewById(R.id.event_subtitle)
        private val iconView: ImageView = view.findViewById(R.id.event_icon)
        private val thumbImageView: ImageView = view.findViewById(R.id.event_thumb_image)
        private val durationView: TextView = view.findViewById(R.id.event_duration)
        private val thumbView: View = view.findViewById(R.id.event_thumb)

        fun bind(item: TimelineEventItem, dateFormat: SimpleDateFormat) {
            val normalized = item.type.lowercase(Locale.US)
            titleView.text = when (normalized) {
                "person-detected", "person" -> "Person Detected"
                "stream-start" -> "Stream Started"
                "stream-stop" -> "Stream Stopped"
                "motion" -> "Motion Detected"
                "night-vision", "night" -> "Night Vision Activated"
                else -> item.type.replace('-', ' ').replaceFirstChar { it.uppercase() }
            }
            timeView.text = dateFormat.format(Date(item.tsMs))
            subtitleView.text = when (normalized) {
                "person-detected", "person" -> "Unknown person at front door"
                "stream-start" -> "Stream is live"
                "stream-stop" -> "Stream ended"
                "motion" -> "Movement at entrance area"
                "night-vision", "night" -> "IR mode auto-switched"
                else -> "Event captured"
            }
            durationView.text = when (normalized) {
                "person-detected", "person" -> "1:15"
                "motion" -> "0:32"
                else -> "0:18"
            }
            iconView.setImageResource(
                when (normalized) {
                    "person-detected", "person" -> R.drawable.ic_rl_person_24
                    "stream-start", "stream-stop" -> R.drawable.ic_rl_videocam_24
                    "motion" -> R.drawable.ic_rl_directions_run_24
                    "night-vision", "night" -> R.drawable.ic_rl_nightlight_24
                    else -> R.drawable.ic_rl_directions_run_24
                },
            )

            val fallbackBg = when (normalized) {
                "person-detected", "person" -> R.drawable.bg_timeline_thumb_alert
                "night-vision", "night" -> R.drawable.bg_timeline_thumb_night
                else -> R.drawable.bg_timeline_thumb
            }
            thumbView.setBackgroundResource(fallbackBg)
            if (!item.thumbnailUrl.isNullOrBlank()) {
                thumbImageView.visibility = View.VISIBLE
                thumbImageView.load(item.thumbnailUrl)
                iconView.visibility = View.GONE
            } else {
                thumbImageView.setImageDrawable(null)
                thumbImageView.visibility = View.GONE
                iconView.visibility = View.VISIBLE
            }

            val dotRes = when (normalized) {
                "person-detected", "person" -> R.drawable.bg_timeline_dot_alert
                "motion" -> R.drawable.bg_timeline_dot_motion
                else -> R.drawable.bg_timeline_dot_default
            }
            dotView.setBackgroundResource(dotRes)
        }
    }
}
