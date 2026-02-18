package com.reallive.android.ui.settings

import android.os.Bundle
import androidx.appcompat.app.AppCompatActivity
import com.google.android.material.appbar.MaterialToolbar
import com.reallive.android.R

class UpgradePlanActivity : AppCompatActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_upgrade_plan)
        findViewById<MaterialToolbar>(R.id.upgrade_toolbar).setNavigationOnClickListener { finish() }
    }
}
