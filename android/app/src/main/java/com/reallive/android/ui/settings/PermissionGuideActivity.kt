package com.reallive.android.ui.settings

import android.os.Bundle
import androidx.appcompat.app.AppCompatActivity
import com.google.android.material.appbar.MaterialToolbar
import com.reallive.android.R

class PermissionGuideActivity : AppCompatActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_permission_guide)
        findViewById<MaterialToolbar>(R.id.permission_toolbar).setNavigationOnClickListener { finish() }
    }
}
