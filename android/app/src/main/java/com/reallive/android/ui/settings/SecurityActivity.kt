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
import android.content.Intent
import androidx.appcompat.app.AlertDialog
import androidx.appcompat.app.AppCompatActivity
import com.reallive.android.R
import com.reallive.android.config.AppConfig
import com.reallive.android.data.CameraRepository
import com.reallive.android.network.ApiClient
import com.reallive.android.network.SettingsResponse
import com.reallive.android.ui.auth.LoginActivity
import androidx.lifecycle.lifecycleScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import retrofit2.HttpException

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
        repository = CameraRepository(ApiClient.create(appConfig.getBaseUrl(), appConfig::getToken))

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
                    val active = repository.getActiveSessions().sessions.size
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
            hint = "New Password (>=6)"
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
                if (next.length < 6) {
                    Toast.makeText(this, "新密码至少6位", Toast.LENGTH_SHORT).show()
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
                val rows = withContext(Dispatchers.IO) { repository.getActiveSessions().sessions }
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
                val labels = rows.map {
                    val camera = it.camera_name ?: "Unknown camera"
                    val status = it.status ?: "active"
                    "$camera (#${it.camera_id}) · $status"
                }.toTypedArray()
                AlertDialog.Builder(this@SecurityActivity)
                    .setTitle("Active Sessions")
                    .setItems(labels) { _, which ->
                        confirmRevokeSession(rows[which].id)
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
                        withContext(Dispatchers.IO) { repository.revokeSession(sessionId) }
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
            appConfig.clearAuth()
            startActivity(android.content.Intent(this, LoginActivity::class.java))
            finish()
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
