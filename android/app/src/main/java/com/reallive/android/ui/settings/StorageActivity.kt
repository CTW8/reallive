package com.reallive.android.ui.settings

import android.content.Intent
import android.os.Bundle
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import com.google.android.material.appbar.MaterialToolbar
import com.google.android.material.button.MaterialButton
import com.reallive.android.R
import com.reallive.android.config.AppConfig

class StorageActivity : AppCompatActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val appConfig = AppConfig(this)
        if (appConfig.getToken().isNullOrBlank()) {
            finish()
            return
        }

        setContentView(R.layout.activity_storage)
        findViewById<MaterialToolbar>(R.id.storage_toolbar).setNavigationOnClickListener { finish() }
        findViewById<MaterialButton>(R.id.storage_upgrade_btn).setOnClickListener {
            startActivity(Intent(this, UpgradePlanActivity::class.java))
        }
        renderMockData()
    }

    private fun renderMockData() {
        findViewById<TextView>(R.id.storage_status).text = "80 / 100 GB used"
        findViewById<TextView>(R.id.storage_segments).text = "412"
        findViewById<TextView>(R.id.storage_events).text = "967"
        findViewById<TextView>(R.id.storage_duration).text = "Front Door 28 GB · Garage 17.6 GB · Living Room 14.4 GB"
        findViewById<TextView>(R.id.storage_sessions).text = "Auto-delete: recordings older than 30 days"
    }
}
