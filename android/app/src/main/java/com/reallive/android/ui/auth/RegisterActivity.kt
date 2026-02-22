package com.reallive.android.ui.auth

import android.content.Intent
import android.os.Bundle
import android.util.Patterns
import android.view.inputmethod.EditorInfo
import android.widget.EditText
import android.widget.ImageView
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import androidx.lifecycle.lifecycleScope
import com.reallive.android.R
import com.reallive.android.config.AppConfig
import com.reallive.android.data.CameraRepository
import com.reallive.android.network.ApiClient
import com.reallive.android.ui.dashboard.DashboardActivity
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import org.json.JSONObject
import retrofit2.HttpException
import java.util.Locale

class RegisterActivity : AppCompatActivity() {
    private lateinit var appConfig: AppConfig
    private lateinit var repository: CameraRepository
    private lateinit var nameContainer: android.view.View
    private lateinit var emailContainer: android.view.View
    private lateinit var phoneContainer: android.view.View
    private lateinit var passwordContainer: android.view.View
    private lateinit var confirmContainer: android.view.View
    private lateinit var usernameInput: EditText
    private lateinit var emailInput: EditText
    private lateinit var phoneInput: EditText
    private lateinit var passwordInput: EditText
    private lateinit var confirmPasswordInput: EditText
    private lateinit var passwordToggle: ImageView
    private lateinit var confirmPasswordToggle: ImageView
    private lateinit var errorText: TextView
    private lateinit var registerButton: TextView
    private var isLoading: Boolean = false
    private var hasAttemptedSubmit: Boolean = false

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_register)

        appConfig = AppConfig(this)
        repository = CameraRepository(ApiClient.create(appConfig.getBaseUrl(), appConfig::getToken))
        if (!appConfig.getToken().isNullOrBlank()) {
            startActivity(Intent(this, DashboardActivity::class.java))
            finish()
            return
        }

        nameContainer = findViewById(R.id.register_name_container)
        emailContainer = findViewById(R.id.register_email_container)
        phoneContainer = findViewById(R.id.register_phone_container)
        passwordContainer = findViewById(R.id.register_password_container)
        confirmContainer = findViewById(R.id.register_confirm_container)
        usernameInput = findViewById(R.id.register_input_username)
        emailInput = findViewById(R.id.register_input_email)
        phoneInput = findViewById(R.id.register_input_phone)
        passwordInput = findViewById(R.id.register_input_password)
        confirmPasswordInput = findViewById(R.id.register_input_confirm_password)
        passwordToggle = findViewById(R.id.register_password_toggle)
        confirmPasswordToggle = findViewById(R.id.register_confirm_password_toggle)
        errorText = findViewById(R.id.register_error)
        registerButton = findViewById(R.id.btn_register)
        findViewById<android.view.View>(R.id.register_back).setOnClickListener { finish() }
        applyLocalizedTexts()

        registerButton.setOnClickListener { register() }
        confirmPasswordInput.setOnEditorActionListener { _, actionId, _ ->
            if (actionId == EditorInfo.IME_ACTION_DONE) {
                register()
                true
            } else {
                false
            }
        }
        usernameInput.setOnEditorActionListener { _, actionId, _ ->
            if (actionId == EditorInfo.IME_ACTION_NEXT) {
                emailInput.requestFocus()
                true
            } else {
                false
            }
        }
        emailInput.setOnEditorActionListener { _, actionId, _ ->
            if (actionId == EditorInfo.IME_ACTION_NEXT) {
                phoneInput.requestFocus()
                true
            } else {
                false
            }
        }
        phoneInput.setOnEditorActionListener { _, actionId, _ ->
            if (actionId == EditorInfo.IME_ACTION_NEXT) {
                passwordInput.requestFocus()
                true
            } else {
                false
            }
        }
        passwordInput.setOnEditorActionListener { _, actionId, _ ->
            if (actionId == EditorInfo.IME_ACTION_NEXT) {
                confirmPasswordInput.requestFocus()
                true
            } else {
                false
            }
        }
        usernameInput.addTextChangedListener(SimpleTextWatcher { validateInputs(showErrors = false) })
        emailInput.addTextChangedListener(SimpleTextWatcher { validateInputs(showErrors = false) })
        phoneInput.addTextChangedListener(SimpleTextWatcher { validateInputs(showErrors = false) })
        passwordInput.addTextChangedListener(SimpleTextWatcher { validateInputs(showErrors = false) })
        confirmPasswordInput.addTextChangedListener(SimpleTextWatcher { validateInputs(showErrors = false) })
        passwordToggle.setOnClickListener { togglePassword(passwordInput, passwordToggle) }
        confirmPasswordToggle.setOnClickListener { togglePassword(confirmPasswordInput, confirmPasswordToggle) }
        validateInputs(showErrors = false)
    }

    private fun register() {
        val username = usernameInput.text.toString().trim()
        val email = emailInput.text.toString().trim()
        val password = passwordInput.text.toString()

        hasAttemptedSubmit = true
        val error = validateInputs(showErrors = true)
        if (error != null) return
        setError(null)
        isLoading = true
        registerButton.isEnabled = false
        lifecycleScope.launch {
            try {
                val response = withContext(Dispatchers.IO) {
                    repository.register(username = username, email = email, password = password)
                }
                appConfig.setToken(response.token)
                appConfig.setUserId(response.user.id)
                appConfig.setUsername(response.user.username)
                appConfig.setEmail(response.user.email)
                startActivity(Intent(this@RegisterActivity, DashboardActivity::class.java))
                finish()
            } catch (ex: Exception) {
                setError(parseError(ex, tr("Registration failed.", "注册失败。")))
            } finally {
                isLoading = false
                registerButton.isEnabled = true
                updateButtonState()
            }
        }
    }

    private fun validateInputs(showErrors: Boolean): String? {
        val username = usernameInput.text.toString().trim()
        val email = emailInput.text.toString().trim()
        val phone = phoneInput.text.toString().trim()
        val password = passwordInput.text.toString()
        val confirm = confirmPasswordInput.text.toString()
        val error = when {
            username.isBlank() -> tr("Full name is required.", "请输入姓名。")
            email.isBlank() -> tr("Email is required.", "请输入邮箱。")
            !Patterns.EMAIL_ADDRESS.matcher(email).matches() -> tr("Enter a valid email.", "请输入有效邮箱。")
            phone.isBlank() -> tr("Phone number is required.", "请输入手机号。")
            password.isBlank() -> tr("Password is required.", "请输入密码。")
            password.length < 8 -> tr("Password must be at least 8 characters.", "密码至少需要 8 位。")
            confirm.isBlank() -> tr("Confirm your password.", "请确认密码。")
            password != confirm -> tr("Passwords do not match.", "两次输入的密码不一致。")
            else -> null
        }
        val nameError = username.isBlank()
        val emailError = email.isBlank() || !Patterns.EMAIL_ADDRESS.matcher(email).matches()
        val phoneError = phone.isBlank()
        val passwordError = password.isBlank() || password.length < 8
        val confirmError = confirm.isBlank() || password != confirm
        val show = showErrors || hasAttemptedSubmit
        updateFieldState(nameContainer, nameError && show)
        updateFieldState(emailContainer, emailError && show)
        updateFieldState(phoneContainer, phoneError && show)
        updateFieldState(passwordContainer, passwordError && show)
        updateFieldState(confirmContainer, confirmError && show)
        if (showErrors || hasAttemptedSubmit) {
            setError(error)
        } else if (error == null) {
            setError(null)
        }
        registerButton.isEnabled = error == null && !isLoading
        updateButtonState()
        return error
    }

    private fun setError(message: String?) {
        if (message.isNullOrBlank()) {
            errorText.text = ""
            errorText.visibility = android.view.View.GONE
        } else {
            errorText.text = message
            errorText.visibility = android.view.View.VISIBLE
        }
    }

    private fun updateFieldState(container: android.view.View, hasError: Boolean) {
        container.setBackgroundResource(
            if (hasError) R.drawable.bg_text_field_error else R.drawable.bg_text_field
        )
    }

    private fun updateButtonState() {
        registerButton.setBackgroundResource(
            if (registerButton.isEnabled) R.drawable.bg_btn_primary else R.drawable.bg_btn_primary_disabled
        )
        registerButton.alpha = if (registerButton.isEnabled) 1f else 0.7f
    }

    private fun togglePassword(input: EditText, icon: ImageView) {
        val isHidden = input.transformationMethod is android.text.method.PasswordTransformationMethod
        if (isHidden) {
            input.transformationMethod = null
            icon.setImageResource(R.drawable.ic_rl_visibility_24)
        } else {
            input.transformationMethod = android.text.method.PasswordTransformationMethod.getInstance()
            icon.setImageResource(R.drawable.ic_rl_visibility_off_24)
        }
        input.setSelection(input.text?.length ?: 0)
    }

    private class SimpleTextWatcher(private val onChange: () -> Unit) : android.text.TextWatcher {
        override fun beforeTextChanged(s: CharSequence?, start: Int, count: Int, after: Int) = Unit
        override fun onTextChanged(s: CharSequence?, start: Int, before: Int, count: Int) = onChange()
        override fun afterTextChanged(s: android.text.Editable?) = Unit
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
        findViewById<TextView>(R.id.register_page_title).text = tr("Create Account", "创建账号")
        findViewById<TextView>(R.id.register_section_personal).text = tr("Personal Information", "个人信息")
        findViewById<TextView>(R.id.register_label_name).text = tr("Full Name", "姓名")
        findViewById<TextView>(R.id.register_label_email).text = tr("Email Address", "邮箱地址")
        findViewById<TextView>(R.id.register_label_phone).text = tr("Phone Number", "手机号")
        findViewById<TextView>(R.id.register_section_security).text = tr("Security", "安全")
        findViewById<TextView>(R.id.register_label_password).text = tr("Password", "密码")
        findViewById<TextView>(R.id.register_label_confirm_password).text = tr("Confirm Password", "确认密码")
        findViewById<TextView>(R.id.register_password_hint).text =
            tr("Min 8 chars with uppercase, lowercase & numbers", "至少 8 位，包含大小写字母和数字")
        findViewById<TextView>(R.id.register_terms_text).text =
            tr("I agree to the Terms of Service and Privacy Policy", "我同意服务条款和隐私政策")
        registerButton.text = tr("Create Account", "创建账号")
    }

    private fun tr(en: String, zh: String): String = if (isChineseLanguage(appConfig.getAppLanguage())) zh else en

    private fun isChineseLanguage(languageCode: String?): Boolean {
        if (languageCode.isNullOrBlank()) return false
        return languageCode.lowercase(Locale.US).startsWith("zh")
    }
}
