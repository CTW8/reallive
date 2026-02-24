package com.reallive.android.ui.auth

import android.os.Bundle
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import androidx.lifecycle.lifecycleScope
import com.reallive.android.R
import com.reallive.android.config.AppConfig
import com.reallive.android.data.CameraRepository
import com.reallive.android.network.ApiFactory
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import org.json.JSONObject
import retrofit2.HttpException
import java.util.Locale

class ForgotPasswordActivity : AppCompatActivity() {
    private lateinit var appConfig: AppConfig
    private lateinit var repository: CameraRepository
    private lateinit var statusText: TextView
    private lateinit var emailInput: android.widget.EditText

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_forgot_password)

        findViewById<android.view.View>(R.id.forgot_back).setOnClickListener { finish() }
        emailInput = findViewById(R.id.forgot_email)
        findViewById<android.widget.EditText>(R.id.forgot_code)
        findViewById<android.widget.EditText>(R.id.forgot_new_password)
        findViewById<android.widget.EditText>(R.id.forgot_confirm_password)
        statusText = findViewById(R.id.forgot_status)

        appConfig = AppConfig(this)
        repository = CameraRepository(ApiFactory.createAuthorized(appConfig))
        applyLocalizedTexts()

        findViewById<android.view.View>(R.id.forgot_send_code).setOnClickListener {
            val email = emailInput.text?.toString()?.trim().orEmpty()
            if (email.isBlank()) {
                statusText.visibility = android.view.View.VISIBLE
                statusText.text = tr("Email is required.", "请输入邮箱。")
                return@setOnClickListener
            }
            statusText.text = ""
            statusText.visibility = android.view.View.GONE
            lifecycleScope.launch {
                try {
                    val resp = withContext(Dispatchers.IO) { repository.forgotPassword(email) }
                    val fallback = tr("Verification code sent.", "验证码已发送。")
                    val msg = resp.message.ifBlank { fallback }
                    statusText.text = msg
                    statusText.visibility = android.view.View.VISIBLE
                } catch (ex: Exception) {
                    statusText.text = parseError(ex, tr("Failed to send verification code.", "发送验证码失败。"))
                    statusText.visibility = android.view.View.VISIBLE
                }
            }
        }

        findViewById<android.view.View>(R.id.forgot_submit).setOnClickListener {
            statusText.text = tr("Password reset is not supported yet.", "暂不支持重置密码。")
            statusText.visibility = android.view.View.VISIBLE
        }
    }

    private fun parseError(ex: Exception, fallback: String): String {
        if (ex is HttpException) {
            val body = ex.response()?.errorBody()?.string()
            if (!body.isNullOrBlank()) {
                runCatching {
                    val json = JSONObject(body)
                    val msg = json.optString("error")
                    if (msg.isNotBlank()) return msg
                }
            }
        }
        return ex.message ?: fallback
    }

    private fun applyLocalizedTexts() {
        findViewById<TextView>(R.id.forgot_page_title).text = tr("Reset Password", "重置密码")
        findViewById<TextView>(R.id.forgot_heading).text = tr("Forgot your password?", "忘记密码？")
        findViewById<TextView>(R.id.forgot_subheading).text =
            tr(
                "Enter your account email and we will send a 6-digit verification code.",
                "输入账号邮箱，我们将发送 6 位验证码。",
            )
        findViewById<TextView>(R.id.forgot_label_email).text = tr("Email Address", "邮箱地址")
        findViewById<TextView>(R.id.forgot_send_code).text = tr("Send Verification Code", "发送验证码")
        findViewById<TextView>(R.id.forgot_label_code).text = tr("Verification Code", "验证码")
        findViewById<TextView>(R.id.forgot_label_new_password).text = tr("New Password", "新密码")
        findViewById<TextView>(R.id.forgot_label_confirm_password).text = tr("Confirm Password", "确认密码")
        findViewById<TextView>(R.id.forgot_submit).text = tr("Reset Password", "重置密码")
        emailInput.hint = tr("Email Address", "邮箱地址")
    }

    private fun zh(): Boolean = isChineseLanguage(appConfig.getAppLanguage())

    private fun tr(en: String, zh: String): String = if (this.zh()) zh else en

    private fun isChineseLanguage(languageCode: String?): Boolean {
        if (languageCode.isNullOrBlank()) return false
        return languageCode.lowercase(Locale.US).startsWith("zh")
    }
}
