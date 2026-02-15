package com.reallive.android.ui.watch

import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.TextView
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
        private val titleView: TextView = view.findViewById(R.id.event_title)
        private val timeView: TextView = view.findViewById(R.id.event_time)
        private val scoreView: TextView = view.findViewById(R.id.event_score)

        fun bind(item: TimelineEventItem, dateFormat: SimpleDateFormat) {
            titleView.text = when (item.type.lowercase()) {
                "person-detected", "person" -> "Person Detected"
                else -> item.type
            }
            timeView.text = dateFormat.format(Date(item.tsMs))
            scoreView.text = "%.2f".format(Locale.US, item.score)
        }
    }
}
