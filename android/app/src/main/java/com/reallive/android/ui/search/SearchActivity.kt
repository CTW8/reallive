package com.reallive.android.ui.search

import android.content.Intent
import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.EditText
import android.widget.ImageView
import android.widget.ImageButton
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import androidx.core.content.ContextCompat
import androidx.core.widget.doOnTextChanged
import androidx.recyclerview.widget.DiffUtil
import androidx.recyclerview.widget.LinearLayoutManager
import androidx.recyclerview.widget.ListAdapter
import androidx.recyclerview.widget.RecyclerView
import com.google.android.material.appbar.MaterialToolbar
import com.google.android.material.button.MaterialButton
import com.reallive.android.R
import com.reallive.android.ui.watch.WatchActivity
import java.util.Locale

class SearchActivity : AppCompatActivity() {
    private lateinit var adapter: SearchResultAdapter
    private lateinit var emptyText: TextView
    private lateinit var input: EditText

    private var query = ""

    private val items = listOf(
        SearchItem("camera-1", SearchType.CAMERA, "Front Door", "Camera · Main Entrance · Online", "live", 1L, "front-door"),
        SearchItem("event-1", SearchType.EVENT, "Motion - Front Door", "Event · Today 09:35 AM", "event", 1L, "front-door"),
        SearchItem("event-2", SearchType.EVENT, "Person - Front Door", "Event · Today 08:52 AM", "event", 1L, "front-door"),
        SearchItem("gallery-1", SearchType.GALLERY, "Front Door Snapshots", "Gallery · 24 photos", "gallery", 1L, "front-door"),
        SearchItem("camera-2", SearchType.CAMERA, "Garage", "Camera · Side Building · Online", "live", 2L, "garage"),
        SearchItem("camera-3", SearchType.CAMERA, "Living Room", "Camera · Indoor · Online", "live", 3L, "living-room"),
    )

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_search)

        emptyText = findViewById(R.id.search_empty)

        val toolbar = findViewById<MaterialToolbar>(R.id.search_toolbar)
        toolbar.setNavigationOnClickListener { finish() }

        val recycler = findViewById<RecyclerView>(R.id.search_recycler)
        adapter = SearchResultAdapter { item -> openWatch(item) }
        recycler.layoutManager = LinearLayoutManager(this)
        recycler.adapter = adapter

        input = findViewById(R.id.search_input)
        input.setText("Front")
        input.doOnTextChanged { text, _, _, _ ->
            query = text?.toString().orEmpty()
            applyFilter()
        }

        findViewById<ImageButton>(R.id.search_clear).setOnClickListener {
            input.setText("")
        }
        findViewById<MaterialButton>(R.id.search_recent_front).setOnClickListener {
            input.setText("Front")
        }
        findViewById<MaterialButton>(R.id.search_recent_garage).setOnClickListener {
            input.setText("Garage")
        }
        findViewById<MaterialButton>(R.id.search_recent_person).setOnClickListener {
            input.setText("Person")
        }

        applyFilter()
    }

    private fun openWatch(item: SearchItem) {
        startActivity(
            Intent(this, WatchActivity::class.java).apply {
                putExtra(WatchActivity.EXTRA_CAMERA_ID, item.cameraId)
                putExtra(WatchActivity.EXTRA_CAMERA_NAME, item.title.replace("Motion - ", "").replace("Person - ", ""))
                putExtra(WatchActivity.EXTRA_STREAM_KEY, item.streamKey)
            },
        )
    }

    private fun applyFilter() {
        val q = query.trim().lowercase(Locale.US)
        val result = items.filter {
            if (q.isBlank()) return@filter true
            it.title.lowercase(Locale.US).contains(q) || it.subtitle.lowercase(Locale.US).contains(q)
        }
        adapter.submitList(result)
        emptyText.visibility = if (result.isEmpty()) View.VISIBLE else View.GONE
    }

    private data class SearchItem(
        val id: String,
        val type: SearchType,
        val title: String,
        val subtitle: String,
        val badge: String,
        val cameraId: Long,
        val streamKey: String,
    )

    private enum class SearchType {
        CAMERA,
        EVENT,
        GALLERY,
    }

    private class SearchResultAdapter(
        private val onClick: (SearchItem) -> Unit,
    ) : ListAdapter<SearchItem, SearchResultAdapter.SearchViewHolder>(DiffCallback()) {
        override fun onCreateViewHolder(parent: ViewGroup, viewType: Int): SearchViewHolder {
            val view = LayoutInflater.from(parent.context)
                .inflate(R.layout.item_search_result, parent, false)
            return SearchViewHolder(view, onClick)
        }

        override fun onBindViewHolder(holder: SearchViewHolder, position: Int) {
            holder.bind(getItem(position))
        }

        class SearchViewHolder(
            itemView: View,
            private val onClick: (SearchItem) -> Unit,
        ) : RecyclerView.ViewHolder(itemView) {
            private val iconView: ImageView = itemView.findViewById(R.id.search_item_icon)
            private val titleText: TextView = itemView.findViewById(R.id.search_item_title)
            private val subtitleText: TextView = itemView.findViewById(R.id.search_item_subtitle)
            private val badgeView: ImageView = itemView.findViewById(R.id.search_item_badge)
            private var item: SearchItem? = null

            init {
                itemView.setOnClickListener { item?.let(onClick) }
            }

            fun bind(item: SearchItem) {
                this.item = item
                titleText.text = item.title
                subtitleText.text = item.subtitle
                when (item.type) {
                    SearchType.CAMERA -> {
                        iconView.setImageResource(R.drawable.ic_videocam_48)
                        iconView.setColorFilter(ContextCompat.getColor(itemView.context, R.color.rl_secondary))
                    }
                    SearchType.EVENT -> {
                        iconView.setImageResource(R.drawable.ic_rl_notifications_active_24)
                        iconView.setColorFilter(ContextCompat.getColor(itemView.context, R.color.rl_warning))
                    }
                    SearchType.GALLERY -> {
                        iconView.setImageResource(R.drawable.ic_rl_photo_24)
                        iconView.setColorFilter(ContextCompat.getColor(itemView.context, R.color.rl_brand))
                    }
                }
                badgeView.setColorFilter(ContextCompat.getColor(itemView.context, R.color.rl_text_secondary))
            }
        }

        private class DiffCallback : DiffUtil.ItemCallback<SearchItem>() {
            override fun areItemsTheSame(oldItem: SearchItem, newItem: SearchItem): Boolean = oldItem.id == newItem.id
            override fun areContentsTheSame(oldItem: SearchItem, newItem: SearchItem): Boolean = oldItem == newItem
        }
    }
}
