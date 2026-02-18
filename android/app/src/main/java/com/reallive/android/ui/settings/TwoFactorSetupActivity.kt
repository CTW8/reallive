package com.reallive.android.ui.settings

import android.os.Bundle
import androidx.appcompat.app.AppCompatActivity
import com.google.android.material.appbar.MaterialToolbar
import com.reallive.android.R

class TwoFactorSetupActivity : AppCompatActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_two_factor_setup)
        findViewById<MaterialToolbar>(R.id.two_factor_toolbar).setNavigationOnClickListener { finish() }
    }
}
