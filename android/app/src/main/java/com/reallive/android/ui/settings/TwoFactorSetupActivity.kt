package com.reallive.android.ui.settings

import android.content.Intent
import android.os.Bundle
import android.widget.EditText
import android.widget.TextView
import android.widget.Toast
import androidx.appcompat.app.AlertDialog
import androidx.appcompat.app.AppCompatActivity
import androidx.lifecycle.lifecycleScope
import com.reallive.android.R
import com.reallive.android.config.AppConfig
import com.reallive.android.data.CameraRepository
import com.reallive.android.network.*
import com.reallive.android.network.SettingsResponse
import com.reallive.android.ui.auth.AuthGuard
import com.reallive.android.ui.auth.LoginActivity
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import retrofit2.HttpException

class TwoFactorSetupActivity : AppCompatActivity() {
    private lateinit var appConfig: AppConfig
    private lateinit var repository: CameraRepository
    private var settings: SettingsResponse? = null
    private var updating = false

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        appConfig = AppConfig(this)
        if (appConfig.getToken().isNullOrBlank()) {
            finish()
            return
        }
        repository = CameraRepository(ApiFactory.createAuthorized(appConfig))
        setContentView(R.layout.activity_two_factor_setup)
        findViewById<android.view.View>(R.id.two_factor_back).setOnClickListener { finish() }
        findViewById<android.view.View>(R.id.two_factor_enable_btn).setOnClickListener { toggleTwoFactor() }
        findViewById<android.view.View>(R.id.two_factor_backup_btn).setOnClickListener {
            showSecurityAuditDialog()
        }
    }

    override fun onStart() {
        super.onStart()
        loadSettings()
    }

    private fun loadSettings() {
        lifecycleScope.launch {
            try {
                val data = withContext(Dispatchers.IO) { repository.getSettings() }
                settings = data
                renderState(data.security.twoFactor)
            } catch (ex: Exception) {
                if (ex is HttpException && ex.code() == 401) {
                    if (recoverUnauthorized()) return@launch
                } else {
                    Toast.makeText(this@TwoFactorSetupActivity, "加载2FA状态失败", Toast.LENGTH_SHORT).show()
                }
            }
        }
    }

    private fun renderState(enabled: Boolean) {
        val status = findViewById<TextView>(R.id.two_factor_status)
        val btn = findViewById<TextView>(R.id.two_factor_enable_btn)
        if (enabled) {
            status.text = "Two-Factor Authentication (Enabled)"
            btn.text = "Disable 2FA"
        } else {
            status.text = "Two-Factor Authentication (Disabled)"
            btn.text = "Verify & Enable 2FA"
        }
    }

    private fun toggleTwoFactor() {
        if (updating) return
        val current = settings ?: return
        val currentlyEnabled = current.security.twoFactor
        if (!currentlyEnabled) {
            val code = findViewById<EditText>(R.id.two_factor_code_input).text?.toString().orEmpty().trim()
            if (code.length != 6) {
                Toast.makeText(this, "请输入6位验证码", Toast.LENGTH_SHORT).show()
                return
            }
        }
        updating = true
        lifecycleScope.launch {
            try {
                val updated = withContext(Dispatchers.IO) {
                    repository.updatePreferences(
                        notifications = current.notifications,
                        system = current.system,
                        security = current.security.copy(twoFactor = !currentlyEnabled),
                    )
                }
                settings = updated
                renderState(updated.security.twoFactor)
                Toast.makeText(
                    this@TwoFactorSetupActivity,
                    if (updated.security.twoFactor) "2FA 已开启" else "2FA 已关闭",
                    Toast.LENGTH_SHORT,
                ).show()
            } catch (ex: Exception) {
                if (ex is HttpException && ex.code() == 401) {
                    if (recoverUnauthorized()) return@launch
                }
                Toast.makeText(this@TwoFactorSetupActivity, "更新2FA失败", Toast.LENGTH_SHORT).show()
            } finally {
                updating = false
            }
        }
    }

    private fun forceRelogin() {
        appConfig.clearAuth()
        startActivity(Intent(this, LoginActivity::class.java))
        finish()
    }

    private fun showSecurityAuditDialog() {
        lifecycleScope.launch {
            try {
                val rows = withContext(Dispatchers.IO) { repository.getSettingsAudit(limit = 20) }
                val securityRows = rows.filter { it.type.equals("security", ignoreCase = true) }
                val message = if (securityRows.isEmpty()) {
                    "No recent security logs"
                } else {
                    securityRows.joinToString("\n\n") { row ->
                        val time = row.time ?: "-"
                        "[$time]\n${row.action}"
                    }
                }
                AlertDialog.Builder(this@TwoFactorSetupActivity)
                    .setTitle("Security Logs")
                    .setMessage(message)
                    .setPositiveButton("OK", null)
                    .show()
            } catch (ex: Exception) {
                if (ex is HttpException && ex.code() == 401) {
                    if (recoverUnauthorized()) return@launch
                }
                Toast.makeText(this@TwoFactorSetupActivity, "获取安全日志失败", Toast.LENGTH_SHORT).show()
            }
        }
    }

    private suspend fun recoverUnauthorized(): Boolean {
        val valid = withContext(Dispatchers.IO) { AuthGuard.isSessionValid(appConfig) }
        if (valid) {
            loadSettings()
            return true
        }
        forceRelogin()
        return true
    }
}
