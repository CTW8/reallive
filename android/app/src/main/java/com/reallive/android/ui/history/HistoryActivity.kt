package com.reallive.android.ui.history

import android.content.Intent
import android.os.Bundle
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import androidx.recyclerview.widget.LinearLayoutManager
import androidx.recyclerview.widget.RecyclerView
import com.google.android.material.appbar.MaterialToolbar
import com.reallive.android.R
import com.reallive.android.ui.watch.CalendarPickerActivity
import com.reallive.android.ui.watch.EventDetailActivity
import com.reallive.android.ui.watch.TimelineEventAdapter
import com.reallive.android.ui.watch.TimelineEventItem

class HistoryActivity : AppCompatActivity() {
    private lateinit var adapter: TimelineEventAdapter

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_history)

        val toolbar = findViewById<MaterialToolbar>(R.id.history_toolbar)
        toolbar.setNavigationOnClickListener { finish() }
        toolbar.inflateMenu(R.menu.menu_history_actions)
        toolbar.setOnMenuItemClickListener { item ->
            if (item.itemId == R.id.action_history_calendar) {
                startActivity(Intent(this, CalendarPickerActivity::class.java))
                true
            } else {
                false
            }
        }

        findViewById<TextView>(R.id.history_status).text = "Front Door"
        findViewById<TextView>(R.id.history_empty).text = "Tap calendar to switch date"

        adapter = TimelineEventAdapter { evt ->
            startActivity(
                Intent(this, EventDetailActivity::class.java).apply {
                    putExtra(EventDetailActivity.EXTRA_EVENT_TYPE, evt.type)
                    putExtra(EventDetailActivity.EXTRA_EVENT_TS, evt.tsMs)
                    putExtra(EventDetailActivity.EXTRA_EVENT_SCORE, evt.score)
                    putExtra(EventDetailActivity.EXTRA_CAMERA_NAME, "Front Door")
                },
            )
        }
        findViewById<RecyclerView>(R.id.history_recycler).apply {
            layoutManager = LinearLayoutManager(this@HistoryActivity)
            adapter = this@HistoryActivity.adapter
        }

        adapter.submitList(
            listOf(
                TimelineEventItem(System.currentTimeMillis() - 2 * 60_000L, "motion", 0.74),
                TimelineEventItem(System.currentTimeMillis() - 8 * 60_000L, "person-detected", 0.92),
                TimelineEventItem(System.currentTimeMillis() - 21 * 60_000L, "motion", 0.68),
                TimelineEventItem(System.currentTimeMillis() - 40 * 60_000L, "person-detected", 0.88),
            ),
        )
    }
}
