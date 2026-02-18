package com.reallive.android.ui.notifications

import android.content.Intent
import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.ImageView
import android.widget.TextView
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import androidx.core.content.ContextCompat
import androidx.recyclerview.widget.DiffUtil
import androidx.recyclerview.widget.LinearLayoutManager
import androidx.recyclerview.widget.ListAdapter
import androidx.recyclerview.widget.RecyclerView
import com.google.android.material.appbar.MaterialToolbar
import com.reallive.android.R
import com.reallive.android.ui.watch.WatchActivity

class NotificationsActivity : AppCompatActivity() {
    private lateinit var adapter: NotificationAdapter

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_notifications)

        findViewById<MaterialToolbar>(R.id.notifications_toolbar).setNavigationOnClickListener { finish() }
        findViewById<TextView>(R.id.notifications_empty).visibility = View.GONE
        findViewById<View>(R.id.notifications_mark_read).setOnClickListener {
            Toast.makeText(this, "Marked all as read (mock)", Toast.LENGTH_SHORT).show()
        }
        findViewById<View>(R.id.notifications_filter).setOnClickListener {
            Toast.makeText(this, "Filter (mock)", Toast.LENGTH_SHORT).show()
        }

        val recycler = findViewById<RecyclerView>(R.id.notifications_recycler)
        adapter = NotificationAdapter { openWatch(it) }
        recycler.layoutManager = LinearLayoutManager(this)
        recycler.adapter = adapter
        adapter.submitList(
            listOf(
                NotificationEntry("1", "Person Detected - Front Door", "An unknown person was detected at the front entrance.", "2 min ago", 1L, "front-door", Level.WARNING),
                NotificationEntry("2", "Motion Detected - Front Door", "Continuous motion detected for 32 seconds.", "15 min ago", 1L, "front-door", Level.WARNING),
                NotificationEntry("3", "Camera Offline - Backyard", "Camera offline since 8:30 AM. Check connection.", "1 hour ago", 4L, "backyard", Level.ERROR),
                NotificationEntry("4", "Motion Detected - Garage", "Brief motion near the garage door.", "3 hours ago", 2L, "garage", Level.INFO),
                NotificationEntry("5", "Firmware Update Available", "Front Door camera v2.4.1 ready.", "5 hours ago", 1L, "front-door", Level.INFO),
                NotificationEntry("6", "Cloud Storage - 80% Used", "Consider upgrading or deleting old recordings.", "Yesterday", 1L, "front-door", Level.INFO),
            ),
        )
    }

    private fun openWatch(entry: NotificationEntry) {
        startActivity(
            Intent(this, WatchActivity::class.java).apply {
                putExtra(WatchActivity.EXTRA_CAMERA_ID, entry.cameraId)
                putExtra(WatchActivity.EXTRA_CAMERA_NAME, entry.title.substringAfterLast("- ").ifBlank { "Camera" })
                putExtra(WatchActivity.EXTRA_STREAM_KEY, entry.streamKey)
            },
        )
    }

    private enum class Level { INFO, WARNING, ERROR }

    private data class NotificationEntry(
        val id: String,
        val title: String,
        val subtitle: String,
        val time: String,
        val cameraId: Long,
        val streamKey: String,
        val level: Level,
    )

    private class NotificationAdapter(
        private val onClick: (NotificationEntry) -> Unit,
    ) : ListAdapter<NotificationEntry, NotificationAdapter.Holder>(Diff()) {
        override fun onCreateViewHolder(parent: ViewGroup, viewType: Int): Holder {
            val view = LayoutInflater.from(parent.context).inflate(R.layout.item_notification, parent, false)
            return Holder(view, onClick)
        }

        override fun onBindViewHolder(holder: Holder, position: Int) = holder.bind(getItem(position))

        class Holder(view: View, private val onClick: (NotificationEntry) -> Unit) : RecyclerView.ViewHolder(view) {
            private val iconView: ImageView = view.findViewById(R.id.notification_icon)
            private val titleText: TextView = view.findViewById(R.id.notification_title)
            private val subtitleText: TextView = view.findViewById(R.id.notification_subtitle)
            private val timeText: TextView = view.findViewById(R.id.notification_time)
            private val thumbView: ImageView = view.findViewById(R.id.notification_thumb)
            private var entry: NotificationEntry? = null

            init {
                itemView.setOnClickListener { entry?.let(onClick) }
            }

            fun bind(item: NotificationEntry) {
                entry = item
                titleText.text = item.title
                subtitleText.text = item.subtitle
                timeText.text = item.time
                when (item.level) {
                    Level.INFO -> {
                        iconView.setImageResource(R.drawable.ic_rl_notifications_24)
                        iconView.setColorFilter(ContextCompat.getColor(itemView.context, R.color.rl_secondary))
                        thumbView.visibility = View.GONE
                    }
                    Level.WARNING -> {
                        iconView.setImageResource(R.drawable.ic_rl_notifications_active_24)
                        iconView.setColorFilter(ContextCompat.getColor(itemView.context, R.color.rl_warning))
                        thumbView.visibility = View.VISIBLE
                    }
                    Level.ERROR -> {
                        iconView.setImageResource(R.drawable.ic_rl_error_24)
                        iconView.setColorFilter(ContextCompat.getColor(itemView.context, R.color.rl_error))
                        thumbView.visibility = View.GONE
                    }
                }
            }
        }

        private class Diff : DiffUtil.ItemCallback<NotificationEntry>() {
            override fun areItemsTheSame(oldItem: NotificationEntry, newItem: NotificationEntry): Boolean = oldItem.id == newItem.id
            override fun areContentsTheSame(oldItem: NotificationEntry, newItem: NotificationEntry): Boolean = oldItem == newItem
        }
    }
}
