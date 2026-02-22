package com.reallive.android.ui.auth

import android.content.Intent
import android.os.Bundle
import android.view.View
import androidx.appcompat.app.AppCompatDelegate
import androidx.appcompat.app.AppCompatActivity
import androidx.core.os.LocaleListCompat
import com.reallive.android.R
import com.reallive.android.config.AppConfig
import com.reallive.android.ui.dashboard.DashboardActivity

class SplashActivity : AppCompatActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val appConfig = AppConfig(this)
        AppCompatDelegate.setApplicationLocales(
            LocaleListCompat.forLanguageTags(appConfig.getAppLanguage()),
        )
        setContentView(R.layout.activity_splash)

        if (!appConfig.getToken().isNullOrBlank()) {
            if (appConfig.shouldRequireReauth()) {
                startActivity(
                    Intent(this, LoginActivity::class.java).apply {
                        putExtra(LoginActivity.EXTRA_FORCE_REAUTH, true)
                    },
                )
            } else {
                appConfig.markAuthenticated()
                startActivity(Intent(this, DashboardActivity::class.java))
            }
            finish()
            return
        }

        findViewById<View>(R.id.splash_btn_start).setOnClickListener {
            startActivity(Intent(this, LoginActivity::class.java))
            finish()
        }
        findViewById<View>(R.id.splash_btn_login).setOnClickListener {
            startActivity(Intent(this, LoginActivity::class.java))
            finish()
        }
    }
}
