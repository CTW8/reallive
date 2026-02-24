package com.reallive.android.ui.settings

import android.os.Bundle
import android.text.InputType
import android.widget.EditText
import android.widget.LinearLayout
import android.widget.Switch
import android.widget.TextView
import android.widget.Toast
import androidx.appcompat.app.AlertDialog
import androidx.appcompat.app.AppCompatActivity
import androidx.lifecycle.lifecycleScope
import com.reallive.android.R
import com.reallive.android.config.AppConfig
import com.reallive.android.data.CameraRepository
import com.reallive.android.network.*
import com.reallive.android.ui.auth.AuthGuard
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import retrofit2.HttpException

class ProfileActivity : AppCompatActivity() {
    private lateinit var appConfig: AppConfig
    private lateinit var repository: CameraRepository
    private var profile: SettingsProfileDto = SettingsProfileDto()
    private var notifications: SettingsNotificationsDto = SettingsNotificationsDto()
    private var system: SettingsSystemDto = SettingsSystemDto()
    private var security: SettingsSecurityDto = SettingsSecurityDto()

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        appConfig = AppConfig(this)
        if (appConfig.getToken().isNullOrBlank()) {
            finish()
            return
        }
        repository = CameraRepository(ApiFactory.createAuthorized(appConfig))

        setContentView(R.layout.activity_profile)
        applyLocalizedTexts()
        findViewById<android.view.View>(R.id.profile_back).setOnClickListener { finish() }
        findViewById<android.view.View>(R.id.profile_edit).setOnClickListener {
            showEditDialog()
        }
        bindItemActions()
    }

    override fun onStart() {
        super.onStart()
        loadProfile()
    }

    private fun loadProfile() {
        lifecycleScope.launch {
            try {
                val settings = withContext(Dispatchers.IO) { repository.getSettings() }
                profile = settings.profile
                notifications = settings.notifications
                system = settings.system
                security = settings.security
                bindProfile(profile)
            } catch (ex: Exception) {
                if (ex is HttpException && ex.code() == 401) {
                    if (handleUnauthorized()) return@launch
                    return@launch
                }
                bindFallbackProfile()
            }
        }
    }

    private fun bindFallbackProfile() {
        val username = appConfig.getUsername().orEmpty().ifBlank { "User" }
        val email = appConfig.getEmail().orEmpty().ifBlank { "unknown@reallive.com" }
        bindProfile(
            SettingsProfileDto(
                displayName = username,
                email = email,
            ),
        )
    }

    private fun bindProfile(data: SettingsProfileDto) {
        findViewById<TextView>(R.id.profile_display_name).text = data.displayName
        findViewById<TextView>(R.id.profile_email).text = data.email
        findViewById<TextView>(R.id.profile_avatar_initial).text = data.displayName.firstOrNull()?.uppercase() ?: "U"
        findViewById<TextView>(R.id.profile_account_full_name).text = data.displayName
        findViewById<TextView>(R.id.profile_account_email).text = data.email
        findViewById<TextView>(R.id.profile_account_email_value).text = data.email
        findViewById<TextView>(R.id.profile_account_phone_value).text = data.phone.ifBlank { "-" }
        findViewById<TextView>(R.id.profile_google_email).text = data.googleEmail.ifBlank { "-" }
        findViewById<TextView>(R.id.profile_google_status).text = if (data.googleLinked) tr("Linked", "已绑定") else tr("Not linked", "未绑定")
        findViewById<TextView>(R.id.profile_bio_status).text = if (security.trustedDevice) tr("Active", "开启") else tr("Off", "关闭")
    }

    private fun bindItemActions() {
        findViewById<android.view.View>(R.id.profile_row_name).setOnClickListener { showEditDialog() }
        findViewById<android.view.View>(R.id.profile_row_email).setOnClickListener { showEditDialog() }
        findViewById<android.view.View>(R.id.profile_row_phone).setOnClickListener { showEditDialog() }
        findViewById<android.view.View>(R.id.profile_row_password).setOnClickListener { showChangePasswordDialog() }
        findViewById<android.view.View>(R.id.profile_row_google).setOnClickListener {
            showGoogleLinkDialog()
        }
        findViewById<android.view.View>(R.id.profile_row_bio).setOnClickListener { showBiometricDialog() }
        findViewById<android.view.View>(R.id.profile_plan_card).setOnClickListener {
            startActivity(android.content.Intent(this, UpgradePlanActivity::class.java))
        }
        findViewById<android.view.View>(R.id.profile_delete_title).setOnClickListener { confirmDeleteAccount() }
    }

    private fun showEditDialog() {
        val zh = appConfig.getAppLanguage().startsWith("zh", true)
        val container = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            val p = 16.dp()
            setPadding(p, p, p, 0)
        }
        val nameInput = EditText(this).apply {
            hint = if (zh) "显示名称" else "Display Name"
            setText(profile.displayName)
            inputType = InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_FLAG_CAP_WORDS
        }
        val emailInput = EditText(this).apply {
            hint = if (zh) "邮箱" else "Email"
            setText(profile.email)
            inputType = InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_VARIATION_EMAIL_ADDRESS
        }
        val phoneInput = EditText(this).apply {
            hint = if (zh) "手机号" else "Phone"
            setText(profile.phone)
            inputType = InputType.TYPE_CLASS_PHONE
        }
        container.addView(nameInput)
        container.addView(emailInput)
        container.addView(phoneInput)

        AlertDialog.Builder(this)
            .setTitle(if (zh) "编辑资料" else "Edit Profile")
            .setView(container)
            .setNegativeButton(if (zh) "取消" else "Cancel", null)
            .setPositiveButton(if (zh) "保存" else "Save") { _, _ ->
                saveProfile(
                    nameInput.text?.toString().orEmpty(),
                    emailInput.text?.toString().orEmpty(),
                    phoneInput.text?.toString().orEmpty(),
                )
            }
            .show()
    }

    private fun saveProfile(displayName: String, email: String, phone: String) {
        val zh = appConfig.getAppLanguage().startsWith("zh", true)
        if (displayName.isBlank() || email.isBlank()) {
            Toast.makeText(this, if (zh) "名称和邮箱不能为空" else "Name and email are required", Toast.LENGTH_SHORT).show()
            return
        }
        lifecycleScope.launch {
            try {
                val updated = withContext(Dispatchers.IO) {
                    repository.updateProfile(
                        displayName = displayName.trim(),
                        email = email.trim(),
                        phone = phone.trim(),
                        signature = profile.signature,
                        language = profile.language.ifBlank { "English" },
                        timezone = profile.timezone.ifBlank { "UTC+08:00" },
                        googleLinked = profile.googleLinked,
                        googleEmail = profile.googleEmail,
                    )
                }
                profile = updated.profile
                bindProfile(profile)
                appConfig.setUsername(profile.displayName)
                appConfig.setEmail(profile.email)
                Toast.makeText(this@ProfileActivity, if (zh) "资料已更新" else "Profile updated", Toast.LENGTH_SHORT).show()
            } catch (ex: Exception) {
                val message = if (ex is HttpException) {
                    when (ex.code()) {
                        400 -> if (zh) "资料输入无效" else "Invalid profile input"
                        409 -> if (zh) "用户名或邮箱已存在" else "Username or email already exists"
                        401 -> {
                            if (handleUnauthorized()) return@launch
                            return@launch
                        }
                        else -> if (zh) "更新资料失败" else "Failed to update profile"
                    }
                } else {
                    if (zh) "更新资料失败" else "Failed to update profile"
                }
                Toast.makeText(this@ProfileActivity, message, Toast.LENGTH_SHORT).show()
            }
        }
    }

    private fun showChangePasswordDialog() {
        val container = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            val p = 16.dp()
            setPadding(p, p, p, 0)
        }
        val oldInput = EditText(this).apply {
            hint = tr("Current Password", "当前密码")
            inputType = InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_VARIATION_PASSWORD
        }
        val newInput = EditText(this).apply {
            hint = tr("New Password (>=6)", "新密码（至少6位）")
            inputType = InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_VARIATION_PASSWORD
        }
        container.addView(oldInput)
        container.addView(newInput)
        AlertDialog.Builder(this)
            .setTitle(tr("Change Password", "修改密码"))
            .setView(container)
            .setNegativeButton(tr("Cancel", "取消"), null)
            .setPositiveButton(tr("Save", "保存")) { _, _ ->
                val oldPwd = oldInput.text?.toString().orEmpty()
                val newPwd = newInput.text?.toString().orEmpty()
                if (oldPwd.isBlank() || newPwd.length < 6) {
                    Toast.makeText(this, tr("Invalid password input", "密码输入不合法"), Toast.LENGTH_SHORT).show()
                } else {
                    changePassword(oldPwd, newPwd)
                }
            }
            .show()
    }

    private fun changePassword(oldPwd: String, newPwd: String) {
        lifecycleScope.launch {
            try {
                withContext(Dispatchers.IO) { repository.changePassword(oldPwd, newPwd) }
                Toast.makeText(this@ProfileActivity, tr("Password updated", "密码已更新"), Toast.LENGTH_SHORT).show()
            } catch (ex: Exception) {
                if (ex is HttpException && ex.code() == 401) {
                    if (handleUnauthorized()) return@launch
                    return@launch
                }
                Toast.makeText(this@ProfileActivity, tr("Password update failed", "密码修改失败"), Toast.LENGTH_SHORT).show()
            }
        }
    }

    private fun showBiometricDialog() {
        val sw = Switch(this).apply {
            isChecked = security.trustedDevice
            text = tr("Enable biometric unlock", "启用生物识别解锁")
        }
        AlertDialog.Builder(this)
            .setTitle(tr("Biometric", "生物识别"))
            .setView(sw)
            .setNegativeButton(tr("Cancel", "取消"), null)
            .setPositiveButton(tr("Apply", "应用")) { _, _ ->
                if (sw.isChecked != security.trustedDevice) {
                    persistSecurityTrustedDevice(sw.isChecked)
                }
            }
            .show()
    }

    private fun persistSecurityTrustedDevice(enabled: Boolean) {
        lifecycleScope.launch {
            try {
                val updatedSecurity = security.copy(trustedDevice = enabled)
                val settings = withContext(Dispatchers.IO) {
                    repository.updatePreferences(notifications, system, updatedSecurity)
                }
                security = settings.security
                notifications = settings.notifications
                system = settings.system
                findViewById<TextView>(R.id.profile_bio_status).text = if (security.trustedDevice) tr("Active", "开启") else tr("Off", "关闭")
            } catch (ex: Exception) {
                if (ex is HttpException && ex.code() == 401) {
                    if (handleUnauthorized()) return@launch
                    return@launch
                }
                Toast.makeText(this@ProfileActivity, tr("Save failed", "保存失败"), Toast.LENGTH_SHORT).show()
            }
        }
    }

    private fun confirmDeleteAccount() {
        AlertDialog.Builder(this)
            .setTitle(tr("Delete Account", "删除账户"))
            .setMessage(tr("This will permanently remove your account and cameras.", "该操作将永久删除你的账户和摄像头数据。"))
            .setNegativeButton(tr("Cancel", "取消"), null)
            .setPositiveButton(tr("Delete", "删除")) { _, _ ->
                lifecycleScope.launch {
                    try {
                        withContext(Dispatchers.IO) { repository.deleteMe() }
                        appConfig.clearAuth()
                        startActivity(android.content.Intent(this@ProfileActivity, com.reallive.android.ui.auth.LoginActivity::class.java))
                        finish()
                    } catch (ex: Exception) {
                        Toast.makeText(
                            this@ProfileActivity,
                            tr("Delete account failed", "删除账户失败"),
                            Toast.LENGTH_SHORT,
                        ).show()
                    }
                }
            }
            .show()
    }

    private fun showGoogleLinkDialog() {
        val linked = profile.googleLinked
        if (linked) {
            AlertDialog.Builder(this)
                .setTitle(tr("Google Account", "Google 账户"))
                .setMessage(
                    tr(
                        "Current linked account: ${profile.googleEmail.ifBlank { "-" }}\nUnlink this account?",
                        "当前绑定账号：${profile.googleEmail.ifBlank { "-" }}\n是否解绑？",
                    ),
                )
                .setNegativeButton(tr("Cancel", "取消"), null)
                .setPositiveButton(tr("Unlink", "解绑")) { _, _ ->
                    persistGoogleLink(false, "")
                }
                .show()
            return
        }

        val input = EditText(this).apply {
            hint = tr("Google email", "Google 邮箱")
            inputType = InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_VARIATION_EMAIL_ADDRESS
        }
        AlertDialog.Builder(this)
            .setTitle(tr("Link Google", "绑定 Google"))
            .setView(input)
            .setNegativeButton(tr("Cancel", "取消"), null)
            .setPositiveButton(tr("Link", "绑定")) { _, _ ->
                val mail = input.text?.toString()?.trim().orEmpty()
                if (!mail.contains("@")) {
                    Toast.makeText(this, tr("Invalid email", "邮箱格式不正确"), Toast.LENGTH_SHORT).show()
                    return@setPositiveButton
                }
                persistGoogleLink(true, mail)
            }
            .show()
    }

    private fun persistGoogleLink(linked: Boolean, email: String) {
        lifecycleScope.launch {
            try {
                val updated = withContext(Dispatchers.IO) {
                    repository.updateProfile(
                        displayName = profile.displayName,
                        email = profile.email,
                        phone = profile.phone,
                        signature = profile.signature,
                        language = profile.language.ifBlank { "English" },
                        timezone = profile.timezone.ifBlank { "UTC+08:00" },
                        googleLinked = linked,
                        googleEmail = if (linked) email else "",
                    )
                }
                profile = updated.profile
                bindProfile(profile)
                Toast.makeText(
                    this@ProfileActivity,
                    if (linked) tr("Google linked", "Google 已绑定") else tr("Google unlinked", "Google 已解绑"),
                    Toast.LENGTH_SHORT,
                ).show()
            } catch (ex: Exception) {
                if (ex is HttpException && ex.code() == 401) {
                    if (handleUnauthorized()) return@launch
                    return@launch
                }
                Toast.makeText(this@ProfileActivity, tr("Operation failed", "操作失败"), Toast.LENGTH_SHORT).show()
            }
        }
    }

    private suspend fun handleUnauthorized(): Boolean {
        val valid = withContext(Dispatchers.IO) { AuthGuard.isSessionValid(appConfig) }
        if (valid) {
            loadProfile()
            return false
        }
        appConfig.clearAuth()
        finish()
        return true
    }

    private fun applyLocalizedTexts() {
        val zh = appConfig.getAppLanguage().startsWith("zh", true)
        if (!zh) return
        findViewById<TextView>(R.id.profile_page_title).text = "个人资料"
        findViewById<TextView>(R.id.profile_plan_title).text = "订阅计划"
        findViewById<TextView>(R.id.profile_plan_desc).text = "最多 16 台摄像头 · 100GB 云存储 · AI 检测"
        findViewById<TextView>(R.id.profile_plan_renew).text = "续费日期：2026-03-15"
        findViewById<TextView>(R.id.profile_section_account).text = "账户"
        findViewById<TextView>(R.id.profile_phone_title).text = "手机号"
        findViewById<TextView>(R.id.profile_change_password_title).text = "修改密码"
        findViewById<TextView>(R.id.profile_change_password_subtitle).text = "上次修改于 30 天前"
        findViewById<TextView>(R.id.profile_section_linked).text = "已关联账户"
        findViewById<TextView>(R.id.profile_google_status).text = "未绑定"
        findViewById<TextView>(R.id.profile_bio_title).text = "生物识别"
        findViewById<TextView>(R.id.profile_bio_subtitle).text = "已启用 Face ID"
        findViewById<TextView>(R.id.profile_bio_status).text = "开启"
        findViewById<TextView>(R.id.profile_delete_title).text = "删除账户"
    }

    private fun tr(en: String, zh: String): String = if (appConfig.getAppLanguage().startsWith("zh", true)) zh else en

    private fun Int.dp(): Int = (this * resources.displayMetrics.density).toInt()
}
