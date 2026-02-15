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

data class TimelineThumbnailItem(
    val tsMs: Long,
    val imageUrl: String,
)

class TimelineThumbnailAdapter(
    private val onClick: (Long) -> Unit,
) : RecyclerView.Adapter<TimelineThumbnailAdapter.ThumbViewHolder>() {

    private val timeFormat = SimpleDateFormat("HH:mm:ss", Locale.getDefault())
    private val items = mutableListOf<TimelineThumbnailItem>()

    fun submitList(data: List<TimelineThumbnailItem>) {
        items.clear()
        items.addAll(data)
        notifyDataSetChanged()
    }

    override fun onCreateViewHolder(parent: ViewGroup, viewType: Int): ThumbViewHolder {
        val view = LayoutInflater.from(parent.context)
            .inflate(R.layout.item_timeline_thumbnail, parent, false)
        return ThumbViewHolder(view)
    }

    override fun getItemCount(): Int = items.size

    override fun onBindViewHolder(holder: ThumbViewHolder, position: Int) {
        val item = items[position]
        holder.bind(item, timeFormat)
        holder.itemView.setOnClickListener { onClick(item.tsMs) }
    }

    class ThumbViewHolder(view: View) : RecyclerView.ViewHolder(view) {
        private val imageView: ImageView = view.findViewById(R.id.thumb_image)
        private val timeView: TextView = view.findViewById(R.id.thumb_time)

        fun bind(item: TimelineThumbnailItem, timeFormat: SimpleDateFormat) {
            timeView.text = timeFormat.format(Date(item.tsMs))
            imageView.load(item.imageUrl) {
                crossfade(true)
            }
        }
    }
}
