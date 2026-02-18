package com.reallive.android.ui.watch

import android.graphics.drawable.GradientDrawable
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.TextView
import androidx.core.content.ContextCompat
import androidx.recyclerview.widget.RecyclerView
import com.reallive.android.R
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

data class TimelineEventItem(
    val tsMs: Long,
    val type: String,
    val score: Double,
)

class TimelineEventAdapter(
    private val onClick: (TimelineEventItem) -> Unit,
) : RecyclerView.Adapter<TimelineEventAdapter.EventViewHolder>() {

    private val items = mutableListOf<TimelineEventItem>()
    private val dateFormat = SimpleDateFormat("yyyy-MM-dd HH:mm:ss", Locale.getDefault())

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
        private val scoreView: TextView = view.findViewById(R.id.event_score)

        fun bind(item: TimelineEventItem, dateFormat: SimpleDateFormat) {
            val normalized = item.type.lowercase(Locale.US)
            titleView.text = when (normalized) {
                "person-detected", "person" -> "Person detected"
                "stream-start" -> "Stream started"
                "stream-stop" -> "Stream stopped"
                else -> item.type.replace('-', ' ')
            }
            timeView.text = dateFormat.format(Date(item.tsMs))
            scoreView.text = if (item.score > 0.0) {
                "${(item.score * 100.0).toInt()}%"
            } else {
                "-"
            }

            val dotColor = when (normalized) {
                "person-detected", "person" -> ContextCompat.getColor(itemView.context, R.color.rl_warning)
                "stream-start" -> ContextCompat.getColor(itemView.context, R.color.rl_success)
                "stream-stop" -> ContextCompat.getColor(itemView.context, R.color.rl_error)
                else -> ContextCompat.getColor(itemView.context, R.color.rl_text_muted)
            }
            dotView.background = GradientDrawable().apply {
                shape = GradientDrawable.OVAL
                setColor(dotColor)
            }
        }
    }
}
