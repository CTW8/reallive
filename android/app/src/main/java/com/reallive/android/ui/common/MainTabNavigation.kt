package com.reallive.android.ui.common

import android.app.Activity
import android.content.Intent
import com.google.android.material.bottomnavigation.BottomNavigationView
import com.reallive.android.R
import com.reallive.android.config.AppConfig
import com.reallive.android.ui.dashboard.DashboardActivity
import com.reallive.android.ui.notifications.NotificationsActivity
import com.reallive.android.ui.settings.SettingsActivity
import java.util.Locale

object MainTabNavigation {
    val TAB_HOME = R.id.nav_home
    val TAB_ALERTS = R.id.nav_history
    val TAB_SETTINGS = R.id.nav_me
    val TAB_HISTORY = TAB_ALERTS
    val TAB_ME = TAB_SETTINGS

    fun setup(activity: Activity, bottomNav: BottomNavigationView, currentTab: Int) {
        applyLocalizedTabTitles(activity, bottomNav)
        bottomNav.selectedItemId = currentTab
        bottomNav.setOnItemSelectedListener { item ->
            if (item.itemId == currentTab) return@setOnItemSelectedListener true
            val target = when (item.itemId) {
                TAB_HOME -> DashboardActivity::class.java
                TAB_ALERTS -> NotificationsActivity::class.java
                TAB_SETTINGS -> SettingsActivity::class.java
                else -> null
            } ?: return@setOnItemSelectedListener false

            activity.startActivity(
                Intent(activity, target).apply {
                    addFlags(Intent.FLAG_ACTIVITY_CLEAR_TOP)
                    addFlags(Intent.FLAG_ACTIVITY_SINGLE_TOP)
                    addFlags(Intent.FLAG_ACTIVITY_NO_ANIMATION)
                },
            )
            activity.finish()
            true
        }
    }

    private fun applyLocalizedTabTitles(activity: Activity, bottomNav: BottomNavigationView) {
        val isZh = isChineseLanguage(AppConfig(activity).getAppLanguage())
        bottomNav.menu.findItem(TAB_HOME)?.title = if (isZh) "主页" else "Dashboard"
        bottomNav.menu.findItem(TAB_ALERTS)?.title = if (isZh) "告警" else "Alerts"
        bottomNav.menu.findItem(TAB_SETTINGS)?.title = if (isZh) "设置" else "Settings"
    }

    private fun isChineseLanguage(languageCode: String?): Boolean {
        if (languageCode.isNullOrBlank()) return false
        return languageCode.lowercase(Locale.US).startsWith("zh")
    }
}
