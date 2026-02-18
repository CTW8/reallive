package com.reallive.android.ui.settings

import android.os.Bundle
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import com.google.android.material.appbar.MaterialToolbar
import com.reallive.android.R
import com.reallive.android.config.AppConfig

class ProfileActivity : AppCompatActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val appConfig = AppConfig(this)
        if (appConfig.getToken().isNullOrBlank()) {
            finish()
            return
        }

        setContentView(R.layout.activity_profile)
        findViewById<MaterialToolbar>(R.id.profile_toolbar).setNavigationOnClickListener { finish() }

        val username = appConfig.getUsername().orEmpty().ifBlank { "Alex Chen" }
        findViewById<TextView>(R.id.profile_display_name).text = username
        findViewById<TextView>(R.id.profile_email).text = "alex@example.com"
        findViewById<TextView>(R.id.profile_plan_line).text = "Up to 16 cameras · 100GB cloud · AI Detection"
    }
}
