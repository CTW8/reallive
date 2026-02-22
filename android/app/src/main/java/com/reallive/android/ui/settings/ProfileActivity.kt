package com.reallive.android.ui.settings

import android.os.Bundle
import android.text.InputType
import android.widget.EditText
import android.widget.LinearLayout
import android.widget.TextView
import android.widget.Toast
import androidx.appcompat.app.AlertDialog
import androidx.appcompat.app.AppCompatActivity
import androidx.lifecycle.lifecycleScope
import com.reallive.android.R
import com.reallive.android.config.AppConfig
import com.reallive.android.data.CameraRepository
import com.reallive.android.network.ApiClient
import com.reallive.android.network.SettingsProfileDto
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import retrofit2.HttpException

class ProfileActivity : AppCompatActivity() {
    private lateinit var appConfig: AppConfig
    private lateinit var repository: CameraRepository
    private var profile: SettingsProfileDto = SettingsProfileDto()

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        appConfig = AppConfig(this)
        if (appConfig.getToken().isNullOrBlank()) {
            finish()
            return
        }
        repository = CameraRepository(ApiClient.create(appConfig.getBaseUrl(), appConfig::getToken))

        setContentView(R.layout.activity_profile)
        applyLocalizedTexts()
        findViewById<android.view.View>(R.id.profile_back).setOnClickListener { finish() }
        findViewById<android.view.View>(R.id.profile_edit).setOnClickListener {
            showEditDialog()
        }
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
                bindProfile(profile)
            } catch (ex: Exception) {
                if (ex is HttpException && ex.code() == 401) {
                    appConfig.clearAuth()
                    finish()
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
        findViewById<TextView>(R.id.profile_account_phone).text = data.phone.ifBlank { "-" }
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
                            appConfig.clearAuth()
                            finish()
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
        findViewById<TextView>(R.id.profile_google_status).text = "已绑定"
        findViewById<TextView>(R.id.profile_bio_title).text = "生物识别"
        findViewById<TextView>(R.id.profile_bio_subtitle).text = "已启用 Face ID"
        findViewById<TextView>(R.id.profile_bio_status).text = "开启"
        findViewById<TextView>(R.id.profile_delete_title).text = "删除账户"
    }

    private fun Int.dp(): Int = (this * resources.displayMetrics.density).toInt()
}
