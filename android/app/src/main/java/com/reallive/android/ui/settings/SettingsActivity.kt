package com.reallive.android.ui.settings

import android.content.Intent
import android.os.Bundle
import androidx.appcompat.app.AppCompatActivity
import com.google.android.material.appbar.MaterialToolbar
import com.google.android.material.materialswitch.MaterialSwitch
import com.reallive.android.R
import com.reallive.android.config.AppConfig

class SettingsActivity : AppCompatActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val appConfig = AppConfig(this)
        if (appConfig.getToken().isNullOrBlank()) {
            finish()
            return
        }

        setContentView(R.layout.activity_settings)
        findViewById<MaterialToolbar>(R.id.settings_toolbar).setNavigationOnClickListener { finish() }

        findViewById<MaterialSwitch>(R.id.settings_push_switch).isChecked = true
        findViewById<MaterialSwitch>(R.id.settings_motion_switch).isChecked = true
        findViewById<MaterialSwitch>(R.id.settings_person_switch).isChecked = true
        findViewById<MaterialSwitch>(R.id.settings_sound_switch).isChecked = false

        findViewById<android.view.View>(R.id.settings_open_profile).setOnClickListener {
            startActivity(Intent(this, ProfileActivity::class.java))
        }
        findViewById<android.view.View>(R.id.settings_open_storage).setOnClickListener {
            startActivity(Intent(this, StorageActivity::class.java))
        }
        findViewById<android.view.View>(R.id.settings_open_security).setOnClickListener {
            startActivity(Intent(this, SecurityActivity::class.java))
        }
        findViewById<android.view.View>(R.id.settings_open_permission).setOnClickListener {
            startActivity(Intent(this, PermissionGuideActivity::class.java))
        }
    }
}
