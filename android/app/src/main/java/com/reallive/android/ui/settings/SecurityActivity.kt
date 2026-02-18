package com.reallive.android.ui.settings

import android.content.Intent
import android.os.Bundle
import android.widget.Button
import androidx.appcompat.app.AppCompatActivity
import com.google.android.material.appbar.MaterialToolbar
import com.google.android.material.materialswitch.MaterialSwitch
import com.reallive.android.R
import com.reallive.android.config.AppConfig
import com.reallive.android.ui.auth.LoginActivity

class SecurityActivity : AppCompatActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val appConfig = AppConfig(this)
        if (appConfig.getToken().isNullOrBlank()) {
            finish()
            return
        }

        setContentView(R.layout.activity_security)
        findViewById<MaterialToolbar>(R.id.security_toolbar).setNavigationOnClickListener { finish() }

        findViewById<MaterialSwitch>(R.id.security_biometric_switch).isChecked = true
        findViewById<MaterialSwitch>(R.id.security_pin_switch).isChecked = true

        findViewById<Button>(R.id.security_open_two_factor).setOnClickListener {
            startActivity(Intent(this, TwoFactorSetupActivity::class.java))
        }
        findViewById<Button>(R.id.security_open_permissions).setOnClickListener {
            startActivity(Intent(this, PermissionGuideActivity::class.java))
        }
        findViewById<Button>(R.id.security_logout).setOnClickListener {
            appConfig.clearAuth()
            startActivity(Intent(this, LoginActivity::class.java))
            finishAffinity()
        }
    }
}
