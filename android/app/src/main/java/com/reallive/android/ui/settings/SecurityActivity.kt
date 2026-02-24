package com.reallive.android.ui.settings

import android.os.Bundle
import android.text.InputType
import android.view.Gravity
import android.view.View
import android.widget.EditText
import android.widget.FrameLayout
import android.widget.LinearLayout
import android.widget.TextView
import android.widget.Toast
import android.widget.ScrollView
import android.content.Intent
import androidx.appcompat.app.AlertDialog
import androidx.appcompat.app.AppCompatActivity
import com.reallive.android.R
import com.reallive.android.config.AppConfig
import com.reallive.android.data.CameraRepository
import com.reallive.android.network.ApiFactory
import com.reallive.android.network.SettingsResponse
import com.reallive.android.ui.auth.AuthGuard
import com.reallive.android.ui.auth.LoginActivity
import androidx.lifecycle.lifecycleScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import retrofit2.HttpException
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import java.util.TimeZone

class SecurityActivity : AppCompatActivity() {
    private lateinit var appConfig: AppConfig
    private lateinit var repository: CameraRepository
    private var settings: SettingsResponse? = null
    private var updatingTwoFactor = false
    private var updatingSecurityPrefs = false
    private var updatingAutoLock = false
    private var activeSessionCount = 0
    private var recentSecurityAction: String = ""

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        appConfig = AppConfig(this)
        if (appConfig.getToken().isNullOrBlank()) {
            finish()
            return
        }
        repository = CameraRepository(ApiFactory.createAuthorized(appConfig))

        setContentView(R.layout.activity_security)
        applyLocalizedTexts()
        findViewById<android.view.View>(R.id.security_back).setOnClickListener { finish() }
        findViewById<android.view.View>(R.id.security_open_two_factor).setOnClickListener {
            startActivity(Intent(this, TwoFactorSetupActivity::class.java))
        }
        findViewById<View>(R.id.security_open_auto_lock).setOnClickListener {
            showAutoLockDialog()
        }
        findViewById<View>(R.id.security_switch_biometric).setOnClickListener {
            toggleTrustedDevice()
        }
        findViewById<View>(R.id.security_switch_app_pin).setOnClickListener {
            toggleIpAllowlist()
        }
        findViewById<android.view.View>(R.id.security_open_change_password).setOnClickListener {
            showChangePasswordDialog()
        }
        findViewById<android.view.View>(R.id.security_open_active_sessions).setOnClickListener {
            showActiveSessionsDialog()
        }
        findViewById<android.view.View>(R.id.security_open_login_history).setOnClickListener {
            showLoginHistoryDialog()
        }
    }

    override fun onStart() {
        super.onStart()
        if (appConfig.shouldRequireReauth()) {
            startActivity(
                Intent(this, LoginActivity::class.java).apply {
                    putExtra(LoginActivity.EXTRA_FORCE_REAUTH, true)
                },
            )
            finish()
            return
        }
        appConfig.markAuthenticated()
        loadSecurity()
    }

    override fun onStop() {
        super.onStop()
        appConfig.markAppBackgrounded()
    }

    private fun loadSecurity() {
        lifecycleScope.launch {
            try {
                val pair = withContext(Dispatchers.IO) {
                    val data = repository.getSettings()
                    val active = repository.getAuthSessions().count { it.active }
                    val audit = repository.getSettingsAudit(limit = 10)
                    Triple(
                        data,
                        active,
                        audit.firstOrNull { it.type.equals("security", true) }?.action.orEmpty(),
                    )
                }
                val data = pair.first
                activeSessionCount = pair.second
                recentSecurityAction = pair.third
                settings = data
                renderSecurity(data)
            } catch (ex: Exception) {
                handleAuthError(ex)
            }
        }
    }

    private fun renderSecurity(data: SettingsResponse) {
        val risk = computeRiskState(data.security, activeSessionCount, recentSecurityAction)
        findViewById<TextView>(R.id.security_score_value).text = risk.score.toString()
        findViewById<TextView>(R.id.security_score_value).setTextColor(risk.color)
        findViewById<TextView>(R.id.security_score_label).text = risk.label
        findViewById<TextView>(R.id.security_score_summary).text = risk.summary
        val status = findViewById<TextView>(R.id.security_two_factor_status)
        if (data.security.twoFactor) {
            status.text = "Enabled"
            status.setTextColor(0xFF7DD881.toInt())
        } else {
            status.text = "Not enabled - Recommended"
            status.setTextColor(0xFFFF9E43.toInt())
        }
        bindSecuritySwitch(
            containerId = R.id.security_switch_biometric,
            knobId = R.id.security_switch_biometric_knob,
            enabled = data.security.trustedDevice,
        )
        bindSecuritySwitch(
            containerId = R.id.security_switch_app_pin,
            knobId = R.id.security_switch_app_pin_knob,
            enabled = data.security.ipAllowlist,
        )
        findViewById<TextView>(R.id.security_biometric_subtitle).text =
            if (data.security.trustedDevice) "Enabled" else "Disabled"
        findViewById<TextView>(R.id.security_pin_subtitle).text =
            if (data.security.ipAllowlist) "Enabled" else "Disabled"
        findViewById<TextView>(R.id.security_active_sessions_subtitle).text =
            "$activeSessionCount devices logged in"
        findViewById<TextView>(R.id.security_auto_lock_subtitle).text =
            formatAutoLockLabel(data.system.autoLockSec)
        appConfig.setAutoLockSec(data.system.autoLockSec)
    }

    private fun computeRiskState(
        security: com.reallive.android.network.SettingsSecurityDto,
        activeSessions: Int,
        recentAction: String,
    ): RiskState {
        var score = 100
        if (!security.twoFactor) score -= 35
        if (!security.trustedDevice) score -= 15
        if (!security.ipAllowlist) score -= 10
        if (activeSessions > 2) score -= ((activeSessions - 2) * 8).coerceAtMost(24)
        score = score.coerceIn(0, 100)
        val label = when {
            score >= 85 -> "Good"
            score >= 60 -> "Warning"
            else -> "Critical"
        }
        val color = when {
            score >= 85 -> 0xFF7DD881.toInt()
            score >= 60 -> 0xFFFF9E43.toInt()
            else -> 0xFFFF6B6B.toInt()
        }
        val actionPart = if (recentAction.isBlank()) "" else " Latest: $recentAction."
        val summary = when {
            score >= 85 -> "Your account security posture is good.$actionPart"
            score >= 60 -> "Security hardening recommended: enable 2FA and reduce active sessions.$actionPart"
            else -> "High risk detected. Enable 2FA immediately and review active sessions.$actionPart"
        }
        return RiskState(score, label, color, summary)
    }

    private fun toggleTwoFactor() {
        if (updatingTwoFactor || updatingSecurityPrefs) return
        val current = settings ?: return
        val next = !current.security.twoFactor
        updatingTwoFactor = true
        lifecycleScope.launch {
            try {
                val updated = withContext(Dispatchers.IO) {
                    repository.updatePreferences(
                        notifications = current.notifications,
                        system = current.system,
                        security = current.security.copy(twoFactor = next),
                    )
                }
                settings = updated
                renderSecurity(updated)
                Toast.makeText(
                    this@SecurityActivity,
                    if (next) "2FA 已开启" else "2FA 已关闭",
                    Toast.LENGTH_SHORT,
                ).show()
            } catch (ex: Exception) {
                if (!handleAuthError(ex)) {
                    Toast.makeText(this@SecurityActivity, "更新 2FA 失败", Toast.LENGTH_SHORT).show()
                }
            } finally {
                updatingTwoFactor = false
            }
        }
    }

    private fun toggleTrustedDevice() {
        if (updatingSecurityPrefs || updatingTwoFactor) return
        val current = settings ?: return
        persistSecurityPrefs(
            next = current.security.copy(trustedDevice = !current.security.trustedDevice),
            okMsg = if (!current.security.trustedDevice) "Biometric Login 已开启" else "Biometric Login 已关闭",
            failMsg = "更新 Biometric Login 失败",
        )
    }

    private fun toggleIpAllowlist() {
        if (updatingSecurityPrefs || updatingTwoFactor) return
        val current = settings ?: return
        persistSecurityPrefs(
            next = current.security.copy(ipAllowlist = !current.security.ipAllowlist),
            okMsg = if (!current.security.ipAllowlist) "App PIN 已开启" else "App PIN 已关闭",
            failMsg = "更新 App PIN 失败",
        )
    }

    private fun persistSecurityPrefs(
        next: com.reallive.android.network.SettingsSecurityDto,
        okMsg: String,
        failMsg: String,
    ) {
        val current = settings ?: return
        updatingSecurityPrefs = true
        lifecycleScope.launch {
            try {
                val updated = withContext(Dispatchers.IO) {
                    repository.updatePreferences(
                        notifications = current.notifications,
                        system = current.system,
                        security = next,
                    )
                }
                settings = updated
                renderSecurity(updated)
                Toast.makeText(this@SecurityActivity, okMsg, Toast.LENGTH_SHORT).show()
            } catch (ex: Exception) {
                if (!handleAuthError(ex)) {
                    Toast.makeText(this@SecurityActivity, failMsg, Toast.LENGTH_SHORT).show()
                }
            } finally {
                updatingSecurityPrefs = false
            }
        }
    }

    private fun showChangePasswordDialog() {
        val container = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            val p = 16.dp()
            setPadding(p, p, p, 0)
        }
        val currentInput = EditText(this).apply {
            hint = "Current Password"
            inputType = InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_VARIATION_PASSWORD
        }
        val newInput = EditText(this).apply {
            hint = "New Password (>=8)"
            inputType = InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_VARIATION_PASSWORD
        }
        val confirmInput = EditText(this).apply {
            hint = "Confirm Password"
            inputType = InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_VARIATION_PASSWORD
        }
        container.addView(currentInput)
        container.addView(newInput)
        container.addView(confirmInput)
        AlertDialog.Builder(this)
            .setTitle("Change Password")
            .setView(container)
            .setNegativeButton("Cancel", null)
            .setPositiveButton("Save") { _, _ ->
                val current = currentInput.text?.toString().orEmpty()
                val next = newInput.text?.toString().orEmpty()
                val confirm = confirmInput.text?.toString().orEmpty()
                if (current.isBlank() || next.isBlank() || confirm.isBlank()) {
                    Toast.makeText(this, "请填写完整", Toast.LENGTH_SHORT).show()
                    return@setPositiveButton
                }
                if (next.length < 8) {
                    Toast.makeText(this, "新密码至少8位", Toast.LENGTH_SHORT).show()
                    return@setPositiveButton
                }
                if (next != confirm) {
                    Toast.makeText(this, "两次输入的新密码不一致", Toast.LENGTH_SHORT).show()
                    return@setPositiveButton
                }
                changePassword(current, next)
            }
            .show()
    }

    private fun changePassword(currentPassword: String, newPassword: String) {
        lifecycleScope.launch {
            try {
                withContext(Dispatchers.IO) { repository.changePassword(currentPassword, newPassword) }
                Toast.makeText(this@SecurityActivity, "密码已更新", Toast.LENGTH_SHORT).show()
            } catch (ex: Exception) {
                if (handleAuthError(ex)) return@launch
                val message = if (ex is HttpException) {
                    when (ex.code()) {
                        400 -> "当前密码错误或新密码不合法"
                        else -> "修改密码失败"
                    }
                } else {
                    "修改密码失败"
                }
                Toast.makeText(this@SecurityActivity, message, Toast.LENGTH_SHORT).show()
            }
        }
    }

    private fun showActiveSessionsDialog() {
        lifecycleScope.launch {
            try {
                val rows = withContext(Dispatchers.IO) {
                    repository.getAuthSessions()
                        .filter { it.active }
                        .sortedWith(
                            compareByDescending<com.reallive.android.network.AuthSessionDto> { it.current }
                                .thenByDescending { parseTimeMs(it.last_seen_at ?: it.created_at) },
                        )
                }
                activeSessionCount = rows.size
                findViewById<TextView>(R.id.security_active_sessions_subtitle).text = "$activeSessionCount devices logged in"
                if (rows.isEmpty()) {
                    AlertDialog.Builder(this@SecurityActivity)
                        .setTitle("Active Sessions")
                        .setMessage("No active session")
                        .setPositiveButton("OK", null)
                        .show()
                    return@launch
                }
                val dialogView = buildSessionDialogContent(rows)
                val hasOthers = rows.any { !it.current }
                AlertDialog.Builder(this@SecurityActivity)
                    .setTitle("Active Sessions")
                    .setView(dialogView)
                    .setNeutralButton(if (hasOthers) "Logout Other Devices" else "Only Current Device") { _, _ ->
                        if (hasOthers) {
                            confirmRevokeOtherSessions()
                        }
                    }
                    .setNegativeButton("Close", null)
                    .show()
            } catch (ex: Exception) {
                if (!handleAuthError(ex)) {
                    Toast.makeText(this@SecurityActivity, "获取会话失败", Toast.LENGTH_SHORT).show()
                }
            }
        }
    }

    private fun confirmRevokeSession(sessionId: Long) {
        AlertDialog.Builder(this)
            .setTitle("Revoke Session")
            .setMessage("End this active session now?")
            .setNegativeButton("Cancel", null)
            .setPositiveButton("Revoke") { _, _ ->
                lifecycleScope.launch {
                    try {
                        withContext(Dispatchers.IO) { repository.revokeAuthSession(sessionId) }
                        Toast.makeText(this@SecurityActivity, "Session revoked", Toast.LENGTH_SHORT).show()
                        showActiveSessionsDialog()
                    } catch (ex: Exception) {
                        if (!handleAuthError(ex)) {
                            Toast.makeText(this@SecurityActivity, "撤销会话失败", Toast.LENGTH_SHORT).show()
                        }
                    }
                }
            }
            .show()
    }

    private fun confirmRevokeOtherSessions() {
        AlertDialog.Builder(this)
            .setTitle("Logout Other Devices")
            .setMessage("This will sign out all devices except current one.")
            .setNegativeButton("Cancel", null)
            .setPositiveButton("Confirm") { _, _ ->
                lifecycleScope.launch {
                    try {
                        withContext(Dispatchers.IO) { repository.revokeOtherAuthSessions() }
                        Toast.makeText(
                            this@SecurityActivity,
                            "Other devices logged out",
                            Toast.LENGTH_SHORT,
                        ).show()
                        showActiveSessionsDialog()
                    } catch (ex: Exception) {
                        if (!handleAuthError(ex)) {
                            Toast.makeText(
                                this@SecurityActivity,
                                "Failed to logout other devices",
                                Toast.LENGTH_SHORT,
                            ).show()
                        }
                    }
                }
            }
            .show()
    }

    private fun showAutoLockDialog() {
        if (updatingAutoLock || updatingTwoFactor || updatingSecurityPrefs) return
        val current = settings ?: return
        val values = intArrayOf(30, 60, 300, 0)
        val options = values.map(::formatAutoLockLabel).toTypedArray()
        val checked = values.indexOf(current.system.autoLockSec).let { if (it >= 0) it else 1 }
        AlertDialog.Builder(this)
            .setTitle("Auto-lock")
            .setSingleChoiceItems(options, checked, null)
            .setPositiveButton("Save") { dialog, _ ->
                val idx = (dialog as AlertDialog).listView.checkedItemPosition
                val nextSec = values.getOrElse(idx) { 60 }
                updateAutoLock(nextSec)
            }
            .setNegativeButton("Cancel", null)
            .show()
    }

    private fun updateAutoLock(nextSec: Int) {
        val current = settings ?: return
        if (current.system.autoLockSec == nextSec) return
        updatingAutoLock = true
        lifecycleScope.launch {
            try {
                val updated = withContext(Dispatchers.IO) {
                    repository.updatePreferences(
                        notifications = current.notifications,
                        system = current.system.copy(autoLockSec = nextSec),
                        security = current.security,
                    )
                }
                settings = updated
                renderSecurity(updated)
                Toast.makeText(this@SecurityActivity, "Auto-lock updated", Toast.LENGTH_SHORT).show()
            } catch (ex: Exception) {
                if (!handleAuthError(ex)) {
                    Toast.makeText(this@SecurityActivity, "更新 Auto-lock 失败", Toast.LENGTH_SHORT).show()
                }
            } finally {
                updatingAutoLock = false
            }
        }
    }

    private fun formatAutoLockLabel(sec: Int): String {
        val zh = appConfig.getAppLanguage().startsWith("zh", true)
        if (sec <= 0) return if (zh) "永不" else "Never"
        if (sec < 60) return if (zh) "${sec} 秒" else "${sec} seconds"
        val minutes = sec / 60
        return if (zh) {
            "${minutes} 分钟"
        } else {
            if (minutes == 1) "1 minute" else "$minutes minutes"
        }
    }

    private fun buildSessionDialogContent(
        rows: List<com.reallive.android.network.AuthSessionDto>,
    ): View {
        val root = ScrollView(this).apply {
            isFillViewport = true
        }
        val list = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(16.dp(), 8.dp(), 16.dp(), 8.dp())
        }
        rows.forEach { session ->
            list.addView(buildSessionCard(session))
        }
        root.addView(
            list,
            FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.MATCH_PARENT,
                FrameLayout.LayoutParams.WRAP_CONTENT,
            ),
        )
        return root
    }

    private fun buildSessionCard(
        session: com.reallive.android.network.AuthSessionDto,
    ): View {
        val card = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(14.dp(), 12.dp(), 14.dp(), 12.dp())
            background = getDrawable(R.drawable.bg_settings_card)
            val lp = LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT,
                LinearLayout.LayoutParams.WRAP_CONTENT,
            )
            lp.bottomMargin = 10.dp()
            layoutParams = lp
        }

        val device = listOf(session.platform, session.device_name, session.app_version)
            .filter { !it.isNullOrBlank() }
            .joinToString(" · ")
            .ifBlank { "Unknown Device" }
        val platformTag = sessionPlatformTag(session.platform, session.user_agent)
        val ip = session.ip_address ?: "-"
        val seen = formatLastSeen(session.last_seen_at ?: session.created_at)

        val title = TextView(this).apply {
            text = if (session.current) "[$platformTag] $device (Current)" else "[$platformTag] $device"
            textSize = 15f
            setTypeface(typeface, android.graphics.Typeface.BOLD)
            setTextColor(0xFFEAEAF0.toInt())
        }
        val subtitle = TextView(this).apply {
            text = "$ip · $seen"
            textSize = 12f
            setTextColor(0xFFAAAAAD.toInt())
            setPadding(0, 4.dp(), 0, 0)
        }
        card.addView(title)
        card.addView(subtitle)

        if (!session.current) {
            val revokeBtn = TextView(this).apply {
                text = "Revoke Session"
                gravity = Gravity.CENTER
                textSize = 12f
                setTextColor(0xFFEAEAF0.toInt())
                background = getDrawable(R.drawable.bg_btn_outline)
                setPadding(10.dp(), 8.dp(), 10.dp(), 8.dp())
                val lp = LinearLayout.LayoutParams(
                    LinearLayout.LayoutParams.WRAP_CONTENT,
                    LinearLayout.LayoutParams.WRAP_CONTENT,
                )
                lp.topMargin = 10.dp()
                layoutParams = lp
                setOnClickListener {
                    confirmRevokeSession(session.id)
                }
            }
            card.addView(revokeBtn)
        }
        return card
    }

    private fun sessionPlatformTag(platform: String?, userAgent: String?): String {
        val p = (platform ?: "").lowercase(Locale.US)
        val ua = (userAgent ?: "").lowercase(Locale.US)
        return when {
            p.contains("android") -> "Android"
            p.contains("ios") || p.contains("iphone") || p.contains("ipad") -> "iOS"
            p.contains("windows") -> "Windows"
            p.contains("mac") -> "macOS"
            p.contains("linux") -> "Linux"
            ua.contains("mobile") -> "Mobile"
            else -> "Device"
        }
    }

    private fun parseTimeMs(raw: String?): Long {
        if (raw.isNullOrBlank()) return 0L
        val primary = runCatching {
            SimpleDateFormat("yyyy-MM-dd'T'HH:mm:ss.SSSX", Locale.US).parse(raw)?.time ?: 0L
        }.getOrNull()
        if (primary != null && primary > 0L) return primary
        val isoNoMs = runCatching {
            SimpleDateFormat("yyyy-MM-dd'T'HH:mm:ssX", Locale.US).parse(raw)?.time ?: 0L
        }.getOrNull()
        if (isoNoMs != null && isoNoMs > 0L) return isoNoMs
        return runCatching {
            SimpleDateFormat("yyyy-MM-dd HH:mm:ss", Locale.US).apply {
                timeZone = TimeZone.getTimeZone("UTC")
            }.parse(raw)?.time ?: 0L
        }.getOrDefault(0L)
    }

    private fun formatLastSeen(raw: String?): String {
        val ms = parseTimeMs(raw)
        if (ms <= 0L) return "last seen: unknown"
        val deltaSec = ((System.currentTimeMillis() - ms) / 1000L).coerceAtLeast(0L)
        return when {
            deltaSec < 60 -> "last seen: just now"
            deltaSec < 3600 -> "last seen: ${deltaSec / 60}m ago"
            deltaSec < 24 * 3600 -> "last seen: ${deltaSec / 3600}h ago"
            else -> {
                val text = SimpleDateFormat("yyyy-MM-dd HH:mm", Locale.getDefault()).format(Date(ms))
                "last seen: $text"
            }
        }
    }

    private fun applyLocalizedTexts() {
        val zh = appConfig.getAppLanguage().startsWith("zh", true)
        if (!zh) return
        findViewById<TextView>(R.id.security_page_title).text = "安全"
        findViewById<TextView>(R.id.security_biometric_title).text = "生物识别登录"
        findViewById<TextView>(R.id.security_biometric_subtitle).text = "人脸 / 指纹"
        findViewById<TextView>(R.id.security_pin_title).text = "应用 PIN"
        findViewById<TextView>(R.id.security_pin_subtitle).text = "6 位 PIN 已启用"
        findViewById<TextView>(R.id.security_two_factor_title).text = "双重认证 (2FA)"
        findViewById<TextView>(R.id.security_two_factor_status).text = "未启用 - 建议开启"
        findViewById<TextView>(R.id.security_auto_lock_title).text = "自动锁定"
        findViewById<TextView>(R.id.security_change_password_title).text = "修改密码"
        findViewById<TextView>(R.id.security_change_password_subtitle).text = "更新账户密码"
        findViewById<TextView>(R.id.security_active_sessions_title).text = "活跃会话"
        findViewById<TextView>(R.id.security_active_sessions_subtitle).text = "正在登录设备"
        findViewById<TextView>(R.id.security_login_history_title).text = "登录历史"
        findViewById<TextView>(R.id.security_login_history_subtitle).text = "查看最近登录活动"
    }

    private fun showLoginHistoryDialog() {
        lifecycleScope.launch {
            try {
                val rows = withContext(Dispatchers.IO) { repository.getSettingsAudit(limit = 30) }
                val msg = if (rows.isEmpty()) {
                    "No audit logs"
                } else {
                    rows.joinToString("\n\n") { row ->
                        val who = row.user.ifBlank { "Unknown" }
                        val time = row.time ?: "-"
                        "[$time]\n$who · ${row.action}"
                    }
                }
                AlertDialog.Builder(this@SecurityActivity)
                    .setTitle("Login History")
                    .setMessage(msg)
                    .setPositiveButton("OK", null)
                    .show()
            } catch (ex: Exception) {
                if (!handleAuthError(ex)) {
                    Toast.makeText(this@SecurityActivity, "获取登录历史失败", Toast.LENGTH_SHORT).show()
                }
            }
        }
    }

    private fun handleAuthError(ex: Exception): Boolean {
        if (ex is HttpException && ex.code() == 401) {
            lifecycleScope.launch {
                val valid = withContext(Dispatchers.IO) { AuthGuard.isSessionValid(appConfig) }
                if (valid) {
                    loadSecurity()
                } else {
                    appConfig.clearAuth()
                    startActivity(android.content.Intent(this@SecurityActivity, LoginActivity::class.java))
                    finish()
                }
            }
            return true
        }
        return false
    }

    private fun bindSecuritySwitch(containerId: Int, knobId: Int, enabled: Boolean) {
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

    private fun Int.dp(): Int = (this * resources.displayMetrics.density).toInt()

    private data class RiskState(
        val score: Int,
        val label: String,
        val color: Int,
        val summary: String,
    )
}
