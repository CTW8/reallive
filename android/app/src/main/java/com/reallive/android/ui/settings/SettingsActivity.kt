package com.reallive.android.ui.settings

import android.Manifest
import android.content.Intent
import android.content.pm.PackageManager
import android.os.Build
import android.os.Bundle
import android.view.Gravity
import android.view.View
import android.widget.FrameLayout
import android.widget.TextView
import android.widget.Toast
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AppCompatDelegate
import androidx.appcompat.app.AlertDialog
import androidx.appcompat.app.AppCompatActivity
import androidx.core.content.ContextCompat
import androidx.core.os.LocaleListCompat
import androidx.lifecycle.lifecycleScope
import com.reallive.android.R
import com.reallive.android.config.AppConfig
import com.reallive.android.data.CameraRepository
import com.reallive.android.network.ApiClient
import com.reallive.android.network.SettingsNotificationsDto
import com.reallive.android.network.SettingsResponse
import com.reallive.android.network.SettingsSecurityDto
import com.reallive.android.network.SettingsSystemDto
import com.reallive.android.ui.auth.LoginActivity
import com.reallive.android.ui.dashboard.DashboardActivity
import com.reallive.android.ui.notifications.NotificationsActivity
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.async
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import retrofit2.HttpException

class SettingsActivity : AppCompatActivity() {
    private lateinit var appConfig: AppConfig
    private lateinit var repository: CameraRepository
    private var currentSettings: SettingsResponse? = null
    private var savingPreferences = false
    private var storageSubtitle: String = "Storage usage unavailable"
    private var cloudTitle: String = "Cloud Storage"
    private var cloudSubtitle: String = "Cloud status unavailable"
    private var motionSensitivity: String = "High"
    private var soundSensitivity: String = "Loud"
    private var pendingEnablePushAfterPermission = false
    private var activeSessionCount: Int = 0
    private var recentSecurityAction: String = ""

    private val notificationPermissionLauncher = registerForActivityResult(
        ActivityResultContracts.RequestPermission(),
    ) { granted ->
        if (granted && pendingEnablePushAfterPermission) {
            pendingEnablePushAfterPermission = false
            onSwitchTapped(SwitchKey.PUSH)
        } else {
            pendingEnablePushAfterPermission = false
            Toast.makeText(this, "通知权限未授予，无法开启 Push", Toast.LENGTH_SHORT).show()
        }
    }

    private val switchViews = listOf(
        SwitchView(R.id.settings_switch_push, R.id.settings_switch_push_knob),
        SwitchView(R.id.settings_switch_motion, R.id.settings_switch_motion_knob),
        SwitchView(R.id.settings_switch_person, R.id.settings_switch_person_knob),
        SwitchView(R.id.settings_switch_sound, R.id.settings_switch_sound_knob),
        SwitchView(R.id.settings_switch_dark_mode, R.id.settings_switch_dark_mode_knob),
    )

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        appConfig = AppConfig(this)
        applyAppLanguage(appConfig.getAppLanguage())
        if (appConfig.getToken().isNullOrBlank()) {
            finish()
            return
        }
        repository = CameraRepository(ApiClient.create(appConfig.getBaseUrl(), appConfig::getToken))

        setContentView(R.layout.activity_settings)
        findViewById<TextView>(R.id.settings_about_version).text = "Version ${resolveVersionName()}"
        bindSwitchActions()

        findViewById<android.view.View>(R.id.settings_open_profile).setOnClickListener {
            startActivity(Intent(this, ProfileActivity::class.java))
        }
        findViewById<android.view.View>(R.id.settings_open_storage).setOnClickListener {
            startActivity(Intent(this, StorageActivity::class.java))
        }
        findViewById<android.view.View>(R.id.settings_open_local_storage).setOnClickListener {
            openLocalStorage()
        }
        findViewById<android.view.View>(R.id.settings_open_language).setOnClickListener {
            showLanguageDialog()
        }
        findViewById<android.view.View>(R.id.settings_open_security).setOnClickListener {
            startActivity(Intent(this, SecurityActivity::class.java))
        }
        findViewById<android.view.View>(R.id.settings_open_about).setOnClickListener {
            showAboutDialog()
        }
        findViewById<View>(R.id.settings_row_motion).setOnClickListener { showMotionSensitivityDialog() }
        findViewById<View>(R.id.settings_row_sound).setOnClickListener { showSoundSensitivityDialog() }
        findViewById<View>(R.id.settings_row_push).setOnClickListener { onSwitchTapped(SwitchKey.PUSH) }
        findViewById<View>(R.id.settings_row_person).setOnClickListener { onSwitchTapped(SwitchKey.PERSON) }
        findViewById<android.view.View>(R.id.settings_sign_out).setOnClickListener {
            appConfig.clearAuth()
            startActivity(
                Intent(this, LoginActivity::class.java).apply {
                    addFlags(Intent.FLAG_ACTIVITY_CLEAR_TOP)
                    addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
                },
            )
            finish()
        }

        findViewById<android.view.View>(R.id.settings_nav_dashboard).setOnClickListener {
            startActivity(
                Intent(this, DashboardActivity::class.java).apply {
                    addFlags(Intent.FLAG_ACTIVITY_CLEAR_TOP)
                    addFlags(Intent.FLAG_ACTIVITY_SINGLE_TOP)
                    addFlags(Intent.FLAG_ACTIVITY_NO_ANIMATION)
                },
            )
            finish()
        }
        findViewById<android.view.View>(R.id.settings_nav_alerts).setOnClickListener {
            startActivity(
                Intent(this, NotificationsActivity::class.java).apply {
                    addFlags(Intent.FLAG_ACTIVITY_CLEAR_TOP)
                    addFlags(Intent.FLAG_ACTIVITY_SINGLE_TOP)
                    addFlags(Intent.FLAG_ACTIVITY_NO_ANIMATION)
                },
            )
            finish()
        }
        findViewById<android.view.View>(R.id.settings_nav_settings).setOnClickListener {
            // stay on current tab
        }
    }

    override fun onStart() {
        super.onStart()
        if (appConfig.shouldRequireReauth()) {
            forceRelogin(forceReauth = true)
            return
        }
        appConfig.markAuthenticated()
        loadSettings()
    }

    override fun onStop() {
        super.onStop()
        appConfig.markAppBackgrounded()
    }

    private fun loadSettings() {
        lifecycleScope.launch {
            try {
                val pair = withContext(Dispatchers.IO) {
                    val settingsDeferred = async { repository.getSettings() }
                    val storageDeferred = async { repository.getStorageOverview() }
                    val cloudDeferred = async { repository.getStorageCloudConfig() }
                    val sessionsDeferred = async { repository.getActiveSessions() }
                    val auditDeferred = async { repository.getSettingsAudit(limit = 10) }
                    LoadBundle(
                        settings = settingsDeferred.await(),
                        storage = storageDeferred.await(),
                        cloud = cloudDeferred.await(),
                        activeSessions = sessionsDeferred.await().sessions.size,
                        recentSecurityAction = auditDeferred.await()
                            .firstOrNull { it.type.equals("security", true) }
                            ?.action
                            .orEmpty(),
                    )
                }
                val settings = pair.settings
                val storage = pair.storage
                val cloud = pair.cloud
                activeSessionCount = pair.activeSessions
                recentSecurityAction = pair.recentSecurityAction
                storageSubtitle = "${storage.usedPercent.toInt()}% of ${formatGb(storage.total)} GB used"
                cloudTitle = if (cloud.enabled) "${cloud.provider.ifBlank { "Cloud" }} Storage" else "Cloud Storage"
                cloudSubtitle = if (cloud.enabled) {
                    val used = formatGb(cloud.usedGb)
                    val total = formatGb(cloud.totalGb)
                    val status = cloud.syncStatus.ifBlank { "Unknown" }
                    "$used/$total GB · $status"
                } else {
                    "Cloud sync disabled"
                }
                currentSettings = settings
                val resolvedLang = resolveLanguageOption(settings.profile.language)
                if (!resolvedLang.code.equals(appConfig.getAppLanguage(), true)) {
                    appConfig.setAppLanguage(resolvedLang.code)
                    applyAppLanguage(resolvedLang.code)
                }
                bindSettings(settings)
                loadDetectionDefaults()
                syncPushStateWithPermission()
            } catch (ex: Exception) {
                if (ex is HttpException && ex.code() == 401) {
                    forceRelogin()
                }
            }
        }
    }

    private fun bindSettings(settings: SettingsResponse) {
        appConfig.setUsername(settings.profile.displayName)
        appConfig.setEmail(settings.profile.email)
        appConfig.setAutoLockSec(settings.system.autoLockSec)
        findViewById<TextView>(R.id.settings_profile_name).text = settings.profile.displayName
        findViewById<TextView>(R.id.settings_profile_email).text = settings.profile.email
        findViewById<TextView>(R.id.settings_profile_initial).text =
            settings.profile.displayName.firstOrNull()?.uppercase() ?: "U"
        findViewById<TextView>(R.id.settings_language_value).text = "Language"
        findViewById<TextView>(R.id.settings_language_subtitle).text =
            resolveLanguageOption(settings.profile.language).label
        applyLocalizedTexts(resolveLanguageOption(settings.profile.language).code)
        val risk = computeSecurityRisk(settings.security, activeSessionCount)
        findViewById<TextView>(R.id.settings_security_subtitle).text = buildSecuritySummary(
            settings.security.twoFactor,
            settings.security.trustedDevice,
            settings.security.ipAllowlist,
            activeSessionCount,
            recentSecurityAction,
        )
        findViewById<TextView>(R.id.settings_security_subtitle).setTextColor(risk.color)
        findViewById<TextView>(R.id.settings_storage_cloud_value).text = cloudTitle
        findViewById<TextView>(R.id.settings_storage_cloud_subtitle).text = "$storageSubtitle · $cloudSubtitle"
        findViewById<TextView>(R.id.settings_local_storage_subtitle).text =
            if (isMediaPermissionGranted()) "SD card access granted" else "Permission required"
        findViewById<TextView>(R.id.settings_push_subtitle).text = when {
            settings.notifications.email && !isNotificationPermissionGranted() -> "Permission required"
            settings.notifications.email -> "Enabled"
            else -> "Receive real-time alerts"
        }
        findViewById<TextView>(R.id.settings_motion_subtitle).text =
            if (settings.notifications.sms) "Enabled · Sensitivity $motionSensitivity" else "Notify on motion detection"
        findViewById<TextView>(R.id.settings_person_subtitle).text =
            switchSubtitle(settings.notifications.webhook, "AI-powered person alerts")
        findViewById<TextView>(R.id.settings_sound_subtitle).text =
            if (settings.notifications.sound) "Enabled · Sensitivity $soundSensitivity" else "Notify on unusual sounds"
        findViewById<TextView>(R.id.settings_dark_mode_subtitle).text =
            if (settings.system.darkMode) "Enabled" else "Disabled"

        bindSwitch(R.id.settings_switch_push, R.id.settings_switch_push_knob, settings.notifications.email)
        bindSwitch(R.id.settings_switch_motion, R.id.settings_switch_motion_knob, settings.notifications.sms)
        bindSwitch(R.id.settings_switch_person, R.id.settings_switch_person_knob, settings.notifications.webhook)
        bindSwitch(R.id.settings_switch_sound, R.id.settings_switch_sound_knob, settings.notifications.sound)
        bindSwitch(R.id.settings_switch_dark_mode, R.id.settings_switch_dark_mode_knob, settings.system.darkMode)
        applyDarkMode(settings.system.darkMode)
    }

    private fun bindSwitchActions() {
        findViewById<View>(R.id.settings_switch_push).setOnClickListener { onSwitchTapped(SwitchKey.PUSH) }
        findViewById<View>(R.id.settings_switch_motion).setOnClickListener { onSwitchTapped(SwitchKey.MOTION) }
        findViewById<View>(R.id.settings_switch_person).setOnClickListener { onSwitchTapped(SwitchKey.PERSON) }
        findViewById<View>(R.id.settings_switch_sound).setOnClickListener { onSwitchTapped(SwitchKey.SOUND) }
        findViewById<View>(R.id.settings_switch_dark_mode).setOnClickListener { onSwitchTapped(SwitchKey.DARK_MODE) }
    }

    private fun onSwitchTapped(key: SwitchKey) {
        if (savingPreferences) return
        val settings = currentSettings ?: return
        if (key == SwitchKey.PUSH && !settings.notifications.email && !isNotificationPermissionGranted()) {
            pendingEnablePushAfterPermission = true
            requestNotificationPermission()
            return
        }
        val updatedNotifications = settings.notifications.copy(
            email = if (key == SwitchKey.PUSH) !settings.notifications.email else settings.notifications.email,
            sms = if (key == SwitchKey.MOTION) !settings.notifications.sms else settings.notifications.sms,
            webhook = if (key == SwitchKey.PERSON) !settings.notifications.webhook else settings.notifications.webhook,
            sound = if (key == SwitchKey.SOUND) !settings.notifications.sound else settings.notifications.sound,
        )
        val updatedSecurity = settings.security.copy(
            twoFactor = settings.security.twoFactor,
        )
        val updatedSystem = settings.system.copy(
            darkMode = if (key == SwitchKey.DARK_MODE) !settings.system.darkMode else settings.system.darkMode,
        )

        val optimistic = settings.copy(
            notifications = updatedNotifications,
            security = updatedSecurity,
            system = updatedSystem,
        )
        currentSettings = optimistic
        bindSettings(optimistic)
        if (key == SwitchKey.DARK_MODE) {
            persistPreferences(settings, updatedNotifications, updatedSystem, updatedSecurity)
        } else {
            persistDetectionProfile(
                previous = settings,
                pushEnabled = updatedNotifications.email,
                motionEnabled = updatedNotifications.sms,
                personEnabled = updatedNotifications.webhook,
                soundEnabled = updatedNotifications.sound,
            )
        }
    }

    private fun persistPreferences(
        previous: SettingsResponse,
        notifications: SettingsNotificationsDto,
        system: SettingsSystemDto,
        security: SettingsSecurityDto,
    ) {
        lifecycleScope.launch {
            savingPreferences = true
            setSwitchesEnabled(false)
            try {
                val updated = withContext(Dispatchers.IO) {
                    repository.updatePreferences(
                        notifications = notifications,
                        system = system,
                        security = security,
                    )
                }
                currentSettings = updated
                bindSettings(updated)
            } catch (ex: Exception) {
                currentSettings = previous
                bindSettings(previous)
                if (ex is HttpException && ex.code() == 401) {
                    forceRelogin()
                    return@launch
                }
                Toast.makeText(this@SettingsActivity, "同步设置失败", Toast.LENGTH_SHORT).show()
            } finally {
                savingPreferences = false
                setSwitchesEnabled(true)
            }
        }
    }

    private fun setSwitchesEnabled(enabled: Boolean) {
        switchViews.forEach { sv ->
            findViewById<View>(sv.containerId).isEnabled = enabled
            findViewById<View>(sv.containerId).alpha = if (enabled) 1f else 0.6f
        }
    }

    private fun bindSwitch(containerId: Int, knobId: Int, enabled: Boolean) {
        val container = findViewById<FrameLayout>(containerId)
        val knob = findViewById<View>(knobId)
        container.setBackgroundResource(if (enabled) R.drawable.bg_switch_on else R.drawable.bg_switch_off)
        knob.setBackgroundResource(if (enabled) R.drawable.bg_switch_knob_on else R.drawable.bg_switch_knob_off)
        val params = knob.layoutParams as FrameLayout.LayoutParams
        params.gravity = if (enabled) Gravity.END or Gravity.CENTER_VERTICAL else Gravity.START or Gravity.CENTER_VERTICAL
        params.marginStart = if (enabled) 0 else 5.dp()
        params.marginEnd = if (enabled) 5.dp() else 0
        knob.layoutParams = params
    }

    private fun buildSecuritySummary(
        twoFactor: Boolean,
        trustedDevice: Boolean,
        ipAllowlist: Boolean,
        activeSessions: Int,
        recentAction: String,
    ): String {
        val parts = ArrayList<String>(3)
        if (twoFactor) parts += "2FA"
        if (trustedDevice) parts += "Trusted devices"
        if (ipAllowlist) parts += "IP allowlist"
        val base = if (parts.isEmpty()) "No extra hardening" else parts.joinToString(", ")
        val sessionsText = if (activeSessions > 0) "· $activeSessions active sessions" else ""
        val actionText = if (recentAction.isNotBlank()) "· $recentAction" else ""
        return "$base $sessionsText $actionText".trim()
    }

    private fun computeSecurityRisk(
        security: SettingsSecurityDto,
        activeSessions: Int,
    ): SecurityRisk {
        var score = 100
        if (!security.twoFactor) score -= 35
        if (!security.trustedDevice) score -= 15
        if (!security.ipAllowlist) score -= 10
        if (activeSessions > 2) score -= ((activeSessions - 2) * 8).coerceAtMost(24)
        score = score.coerceIn(0, 100)
        val color = when {
            score >= 85 -> 0xFF7DD881.toInt()
            score >= 60 -> 0xFFFF9E43.toInt()
            else -> 0xFFFF6B6B.toInt()
        }
        return SecurityRisk(score, color)
    }

    private fun resolveVersionName(): String {
        return runCatching {
            packageManager.getPackageInfo(packageName, 0).versionName ?: "1.0.0"
        }.getOrDefault("1.0.0")
    }

    private fun showLanguageDialog() {
        val settings = currentSettings ?: return
        val currentOption = resolveLanguageOption(settings.profile.language)
        val options = LANGUAGE_OPTIONS.map { it.label }.toTypedArray()
        val current = LANGUAGE_OPTIONS.indexOfFirst { it.code == currentOption.code }.let { if (it >= 0) it else 0 }
        AlertDialog.Builder(this)
            .setTitle("Select Language")
            .setSingleChoiceItems(options, current) { dialog, which ->
                dialog.dismiss()
                updateLanguage(LANGUAGE_OPTIONS[which])
            }
            .setNegativeButton("Cancel", null)
            .show()
    }

    private fun showMotionSensitivityDialog() {
        val settings = currentSettings ?: return
        val options = arrayOf("Low", "Medium", "High")
        val current = options.indexOf(motionSensitivity).let { if (it >= 0) it else 2 }
        AlertDialog.Builder(this)
            .setTitle("Motion Sensitivity")
            .setSingleChoiceItems(options, current) { dialog, which ->
                dialog.dismiss()
                val previous = settings
                motionSensitivity = options[which]
                bindSettings(settings)
                persistDetectionProfile(
                    previous = previous,
                    pushEnabled = settings.notifications.email,
                    motionEnabled = settings.notifications.sms,
                    personEnabled = settings.notifications.webhook,
                    soundEnabled = settings.notifications.sound,
                )
            }
            .setNegativeButton("Cancel", null)
            .show()
    }

    private fun showSoundSensitivityDialog() {
        val settings = currentSettings ?: return
        val options = arrayOf("Quiet", "Normal", "Loud")
        val current = options.indexOf(soundSensitivity).let { if (it >= 0) it else 2 }
        AlertDialog.Builder(this)
            .setTitle("Sound Sensitivity")
            .setSingleChoiceItems(options, current) { dialog, which ->
                dialog.dismiss()
                val previous = settings
                soundSensitivity = options[which]
                val optimistic = settings.copy(
                    notifications = settings.notifications.copy(sound = true),
                )
                currentSettings = optimistic
                bindSettings(optimistic)
                persistDetectionProfile(
                    previous = previous,
                    pushEnabled = optimistic.notifications.email,
                    motionEnabled = optimistic.notifications.sms,
                    personEnabled = optimistic.notifications.webhook,
                    soundEnabled = optimistic.notifications.sound,
                )
            }
            .setNegativeButton("Cancel", null)
            .show()
    }

    private fun persistDetectionProfile(
        previous: SettingsResponse,
        pushEnabled: Boolean,
        motionEnabled: Boolean,
        personEnabled: Boolean,
        soundEnabled: Boolean,
    ) {
        lifecycleScope.launch {
            savingPreferences = true
            setSwitchesEnabled(false)
            try {
                val updated = withContext(Dispatchers.IO) {
                    repository.updateDetectionProfile(
                        pushEnabled = pushEnabled,
                        motionEnabled = motionEnabled,
                        personEnabled = personEnabled,
                        soundEnabled = soundEnabled,
                        motionSensitivity = motionSensitivity,
                        soundSensitivity = soundSensitivity,
                        applyAllCameras = true,
                    )
                }
                currentSettings = SettingsResponse(
                    profile = updated.profile,
                    notifications = updated.notifications,
                    system = updated.system,
                    security = updated.security,
                    updatedAt = updated.updatedAt,
                    auditLogs = updated.auditLogs,
                )
                bindSettings(currentSettings ?: previous)
                if (updated.detection.camerasUpdated > 0) {
                    Toast.makeText(
                        this@SettingsActivity,
                        "已同步 ${updated.detection.camerasUpdated} 台摄像头",
                        Toast.LENGTH_SHORT,
                    ).show()
                }
            } catch (ex: Exception) {
                currentSettings = previous
                bindSettings(previous)
                if (ex is HttpException && ex.code() == 401) {
                    forceRelogin()
                    return@launch
                }
                Toast.makeText(this@SettingsActivity, "同步检测配置失败", Toast.LENGTH_SHORT).show()
            } finally {
                savingPreferences = false
                setSwitchesEnabled(true)
            }
        }
    }

    private fun loadDetectionDefaults() {
        lifecycleScope.launch {
            try {
                val cameras = withContext(Dispatchers.IO) { repository.listCameras() }
                val firstId = cameras.firstOrNull()?.id ?: return@launch
                val cameraSettings = withContext(Dispatchers.IO) { repository.getCameraSettings(firstId) }
                motionSensitivity = cameraSettings.settings.motion_sensitivity
                soundSensitivity = cameraSettings.settings.sound_sensitivity
                currentSettings?.let { bindSettings(it) }
            } catch (_: Exception) {
            }
        }
    }

    private fun updateLanguage(language: LanguageOption) {
        val settings = currentSettings ?: return
        lifecycleScope.launch {
            try {
                val updated = withContext(Dispatchers.IO) {
                    repository.updateProfile(
                        displayName = settings.profile.displayName,
                        email = settings.profile.email,
                        phone = settings.profile.phone,
                        signature = settings.profile.signature,
                        language = language.code,
                        timezone = settings.profile.timezone.ifBlank { "UTC+08:00" },
                    )
                }
                currentSettings = updated
                appConfig.setAppLanguage(language.code)
                applyAppLanguage(language.code)
                bindSettings(updated)
                recreate()
            } catch (ex: Exception) {
                if (ex is HttpException && ex.code() == 401) {
                    forceRelogin()
                    return@launch
                }
                Toast.makeText(this@SettingsActivity, "更新语言失败", Toast.LENGTH_SHORT).show()
            }
        }
    }

    private fun resolveLanguageOption(raw: String): LanguageOption {
        val normalized = raw.trim()
        if (normalized.isBlank()) return LANGUAGE_OPTIONS.first()
        return LANGUAGE_OPTIONS.firstOrNull { option ->
            option.code.equals(normalized, true) || option.aliases.any { it.equals(normalized, true) }
        } ?: LanguageOption(code = normalized, label = normalized, aliases = listOf(normalized))
    }

    private fun applyAppLanguage(code: String) {
        val tag = normalizeLanguageTag(code)
        AppCompatDelegate.setApplicationLocales(LocaleListCompat.forLanguageTags(tag))
    }

    private fun normalizeLanguageTag(code: String): String {
        val c = code.trim()
        if (c.isBlank()) return "en"
        return when {
            c.equals("english", true) -> "en"
            c.equals("中文", true) || c.equals("简体中文", true) -> "zh-CN"
            c.equals("繁體中文", true) -> "zh-TW"
            c.equals("japanese", true) || c.equals("日本語", true) -> "ja"
            c.equals("korean", true) || c.equals("한국어", true) -> "ko"
            else -> c
        }
    }

    private fun applyLocalizedTexts(languageCode: String) {
        val zh = languageCode.startsWith("zh", ignoreCase = true)
        val t = if (zh) SettingsTexts.zh() else SettingsTexts.en()
        findViewById<TextView>(R.id.settings_page_title).text = t.pageTitle
        findViewById<TextView>(R.id.settings_section_notifications).text = t.sectionNotifications
        findViewById<TextView>(R.id.settings_push_title).text = t.pushTitle
        findViewById<TextView>(R.id.settings_motion_title).text = t.motionTitle
        findViewById<TextView>(R.id.settings_person_title).text = t.personTitle
        findViewById<TextView>(R.id.settings_sound_title).text = t.soundTitle
        findViewById<TextView>(R.id.settings_section_storage).text = t.sectionStorage
        findViewById<TextView>(R.id.settings_local_storage_title).text = t.localStorageTitle
        findViewById<TextView>(R.id.settings_section_general).text = t.sectionGeneral
        findViewById<TextView>(R.id.settings_dark_mode_title).text = t.darkModeTitle
        findViewById<TextView>(R.id.settings_language_value).text = t.languageTitle
        findViewById<TextView>(R.id.settings_security_title).text = t.securityTitle
        findViewById<TextView>(R.id.settings_about_title).text = t.aboutTitle
        findViewById<TextView>(R.id.settings_sign_out_title).text = t.signOutTitle
        findViewById<TextView>(R.id.settings_nav_settings_title).text = t.pageTitle
    }

    private fun openLocalStorage() {
        if (!isMediaPermissionGranted()) {
            startActivity(Intent(this, PermissionGuideActivity::class.java))
            return
        }
        startActivity(Intent(this, StorageActivity::class.java))
    }

    private fun isMediaPermissionGranted(): Boolean {
        return if (Build.VERSION.SDK_INT >= 33) {
            isPermissionGranted(Manifest.permission.READ_MEDIA_VIDEO)
        } else {
            isPermissionGranted(Manifest.permission.READ_EXTERNAL_STORAGE)
        }
    }

    private fun isNotificationPermissionGranted(): Boolean {
        if (Build.VERSION.SDK_INT < 33) return true
        return isPermissionGranted(Manifest.permission.POST_NOTIFICATIONS)
    }

    private fun isPermissionGranted(permission: String): Boolean {
        return ContextCompat.checkSelfPermission(this, permission) == PackageManager.PERMISSION_GRANTED
    }

    private fun requestNotificationPermission() {
        if (Build.VERSION.SDK_INT < 33) return
        notificationPermissionLauncher.launch(Manifest.permission.POST_NOTIFICATIONS)
    }

    private fun syncPushStateWithPermission() {
        val settings = currentSettings ?: return
        if (!settings.notifications.email) return
        if (isNotificationPermissionGranted()) return
        val updatedNotifications = settings.notifications.copy(email = false)
        val optimistic = settings.copy(notifications = updatedNotifications)
        currentSettings = optimistic
        bindSettings(optimistic)
        persistDetectionProfile(
            previous = settings,
            pushEnabled = false,
            motionEnabled = updatedNotifications.sms,
            personEnabled = updatedNotifications.webhook,
            soundEnabled = updatedNotifications.sound,
        )
    }

    private fun showAboutDialog() {
        lifecycleScope.launch {
            val msg = try {
                val health = withContext(Dispatchers.IO) { repository.getHealth() }
                "Version ${resolveVersionName()}\nServer: ${appConfig.getBaseUrl()}\nStatus: ${health.status}\nUptime: ${health.uptime}s\nNode: ${health.nodeVersion ?: "-"}"
            } catch (_: Exception) {
                "Version ${resolveVersionName()}\nServer: ${appConfig.getBaseUrl()}"
            }
            AlertDialog.Builder(this@SettingsActivity)
                .setTitle("About RealLive")
                .setMessage(msg)
                .setPositiveButton("OK", null)
                .show()
        }
    }

    private fun applyDarkMode(enabled: Boolean) {
        AppCompatDelegate.setDefaultNightMode(
            if (enabled) AppCompatDelegate.MODE_NIGHT_YES else AppCompatDelegate.MODE_NIGHT_NO,
        )
    }

    private fun switchSubtitle(enabled: Boolean, offHint: String): String {
        return if (enabled) "Enabled" else offHint
    }

    private fun formatGb(v: Double): String {
        val rounded = kotlin.math.round(v * 10.0) / 10.0
        val asInt = rounded.toInt().toDouble()
        return if (rounded == asInt) rounded.toInt().toString() else rounded.toString()
    }

    private fun forceRelogin(forceReauth: Boolean = false) {
        if (!forceReauth) appConfig.clearAuth()
        startActivity(
            Intent(this@SettingsActivity, LoginActivity::class.java).apply {
                addFlags(Intent.FLAG_ACTIVITY_CLEAR_TOP)
                addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
                if (forceReauth) putExtra(LoginActivity.EXTRA_FORCE_REAUTH, true)
            },
        )
        finish()
    }

    private data class SwitchView(
        val containerId: Int,
        val knobId: Int,
    )

    private data class LoadBundle(
        val settings: SettingsResponse,
        val storage: com.reallive.android.network.StorageOverviewDto,
        val cloud: com.reallive.android.network.StorageCloudDto,
        val activeSessions: Int,
        val recentSecurityAction: String,
    )

    private data class SecurityRisk(
        val score: Int,
        val color: Int,
    )

    private data class SettingsTexts(
        val pageTitle: String,
        val sectionNotifications: String,
        val pushTitle: String,
        val motionTitle: String,
        val personTitle: String,
        val soundTitle: String,
        val sectionStorage: String,
        val localStorageTitle: String,
        val sectionGeneral: String,
        val darkModeTitle: String,
        val languageTitle: String,
        val securityTitle: String,
        val aboutTitle: String,
        val signOutTitle: String,
    ) {
        companion object {
            fun en() = SettingsTexts(
                pageTitle = "Settings",
                sectionNotifications = "Notifications",
                pushTitle = "Push Notifications",
                motionTitle = "Motion Alerts",
                personTitle = "Person Detection",
                soundTitle = "Sound Alerts",
                sectionStorage = "Storage",
                localStorageTitle = "Local Storage",
                sectionGeneral = "General",
                darkModeTitle = "Dark Mode",
                languageTitle = "Language",
                securityTitle = "Security",
                aboutTitle = "About",
                signOutTitle = "Sign Out",
            )

            fun zh() = SettingsTexts(
                pageTitle = "设置",
                sectionNotifications = "通知",
                pushTitle = "推送通知",
                motionTitle = "移动告警",
                personTitle = "人物检测",
                soundTitle = "声音告警",
                sectionStorage = "存储",
                localStorageTitle = "本地存储",
                sectionGeneral = "通用",
                darkModeTitle = "深色模式",
                languageTitle = "语言",
                securityTitle = "安全",
                aboutTitle = "关于",
                signOutTitle = "退出登录",
            )
        }
    }

    private data class LanguageOption(
        val code: String,
        val label: String,
        val aliases: List<String>,
    )

    companion object {
        private val LANGUAGE_OPTIONS = listOf(
            LanguageOption("en", "English", listOf("English", "en-US", "en-GB")),
            LanguageOption("zh-CN", "简体中文", listOf("中文", "简体中文", "zh")),
            LanguageOption("zh-TW", "繁體中文", listOf("繁體中文")),
            LanguageOption("ja", "日本語", listOf("Japanese", "日本語")),
            LanguageOption("ko", "한국어", listOf("Korean", "한국어")),
            LanguageOption("es", "Español", listOf("Spanish", "Español")),
            LanguageOption("fr", "Français", listOf("French", "Français")),
            LanguageOption("de", "Deutsch", listOf("German", "Deutsch")),
            LanguageOption("pt-BR", "Português (Brasil)", listOf("Portuguese", "Português")),
            LanguageOption("ru", "Русский", listOf("Russian", "Русский")),
            LanguageOption("ar", "العربية", listOf("Arabic", "العربية")),
            LanguageOption("hi", "हिन्दी", listOf("Hindi", "हिन्दी")),
        )
    }

    private enum class SwitchKey {
        PUSH,
        MOTION,
        PERSON,
        SOUND,
        DARK_MODE,
    }

    private fun Int.dp(): Int = (this * resources.displayMetrics.density).toInt()
}
