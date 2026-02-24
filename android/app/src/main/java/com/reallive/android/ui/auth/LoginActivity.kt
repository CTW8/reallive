package com.reallive.android.ui.auth

import android.content.Intent
import android.os.Bundle
import android.view.View
import android.view.inputmethod.EditorInfo
import android.widget.EditText
import android.widget.ImageView
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import androidx.lifecycle.lifecycleScope
import com.reallive.android.R
import com.reallive.android.config.AppConfig
import com.reallive.android.data.CameraRepository
import com.reallive.android.network.ApiFactory
import com.reallive.android.ui.dashboard.DashboardActivity
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import org.json.JSONObject
import retrofit2.HttpException
import java.util.Locale

class LoginActivity : AppCompatActivity() {
    private lateinit var appConfig: AppConfig
    private lateinit var repository: CameraRepository
    private lateinit var emailContainer: View
    private lateinit var passwordContainer: View
    private lateinit var usernameInput: EditText
    private lateinit var passwordInput: EditText
    private lateinit var passwordToggle: ImageView
    private lateinit var errorText: TextView
    private lateinit var loginButton: View
    private lateinit var goRegisterButton: TextView
    private lateinit var forgotPasswordButton: TextView
    private lateinit var googleLoginButton: View
    private lateinit var biometricLoginButton: View
    private lateinit var loadingView: View
    private var isLoading: Boolean = false
    private var hasAttemptedSubmit: Boolean = false
    private var forceReauth: Boolean = false

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_login)

        appConfig = AppConfig(this)
        repository = CameraRepository(ApiFactory.createAuthorized(appConfig))
        forceReauth = intent.getBooleanExtra(EXTRA_FORCE_REAUTH, false)
        if (!forceReauth && !appConfig.getToken().isNullOrBlank()) {
            startActivity(Intent(this, DashboardActivity::class.java))
            finish()
            return
        }

        emailContainer = findViewById(R.id.login_email_container)
        passwordContainer = findViewById(R.id.login_password_container)
        usernameInput = findViewById(R.id.input_username)
        passwordInput = findViewById(R.id.input_password)
        usernameInput.setSelectAllOnFocus(false)
        passwordInput.setSelectAllOnFocus(false)
        enforceCursorAtEndOnFocus(usernameInput)
        enforceCursorAtEndOnFocus(passwordInput)
        passwordToggle = findViewById(R.id.login_password_toggle)
        errorText = findViewById(R.id.login_error)
        loginButton = findViewById(R.id.btn_login)
        goRegisterButton = findViewById(R.id.btn_go_register)
        forgotPasswordButton = findViewById(R.id.btn_forgot_password)
        googleLoginButton = findViewById(R.id.btn_login_google)
        biometricLoginButton = findViewById(R.id.btn_login_biometric)
        loadingView = findViewById(R.id.login_loading)

        loginButton.setOnClickListener {
            login()
        }
        passwordInput.setOnEditorActionListener { _, actionId, _ ->
            if (actionId == EditorInfo.IME_ACTION_DONE) {
                login()
                true
            } else {
                false
            }
        }
        usernameInput.setOnEditorActionListener { _, actionId, _ ->
            if (actionId == EditorInfo.IME_ACTION_NEXT) {
                passwordInput.requestFocus()
                true
            } else {
                false
            }
        }
        usernameInput.addTextChangedListener(SimpleTextWatcher { validateInputs(showErrors = false) })
        passwordInput.addTextChangedListener(SimpleTextWatcher { validateInputs(showErrors = false) })
        passwordToggle.setOnClickListener { togglePassword(passwordInput, passwordToggle) }
        goRegisterButton.setOnClickListener {
            startActivity(Intent(this, RegisterActivity::class.java))
        }
        forgotPasswordButton.setOnClickListener {
            startActivity(Intent(this, ForgotPasswordActivity::class.java))
        }
        googleLoginButton.visibility = View.GONE
        biometricLoginButton.visibility = View.GONE
        validateInputs(showErrors = false)
    }

    private fun login() {
        val username = usernameInput.text.toString().trim()
        val password = passwordInput.text.toString()

        hasAttemptedSubmit = true
        val error = validateInputs(showErrors = true)
        if (error != null) return

        setLoading(true)
        setError(null)
        lifecycleScope.launch {
            try {
                val response = withContext(Dispatchers.IO) {
                    repository.login(username = username, password = password)
                }
                appConfig.setAuthTokens(response.token, response.refreshToken)
                appConfig.setUserId(response.user.id)
                appConfig.setUsername(response.user.username)
                appConfig.setEmail(response.user.email)
                appConfig.markAuthenticated()
                startActivity(Intent(this@LoginActivity, DashboardActivity::class.java))
                finish()
            } catch (ex: Exception) {
                setError(parseError(ex, tr("Login failed.", "登录失败。")))
            } finally {
                setLoading(false)
            }
        }
    }

    private fun setLoading(loading: Boolean) {
        isLoading = loading
        loginButton.isEnabled = !loading
        goRegisterButton.isEnabled = !loading
        forgotPasswordButton.isEnabled = !loading
        googleLoginButton.isEnabled = !loading
        biometricLoginButton.isEnabled = !loading
        loadingView.visibility = if (loading) View.VISIBLE else View.GONE
        updateButtonState()
    }

    private fun validateInputs(showErrors: Boolean): String? {
        val identifier = usernameInput.text.toString().trim()
        val password = passwordInput.text.toString()
        val error = when {
            identifier.isBlank() -> tr("Email or username is required.", "请输入邮箱或用户名。")
            password.isBlank() -> tr("Password is required.", "请输入密码。")
            else -> null
        }
        val emailError = identifier.isBlank()
        val passwordError = password.isBlank()
        updateFieldState(emailContainer, emailError && (showErrors || hasAttemptedSubmit))
        updateFieldState(passwordContainer, passwordError && (showErrors || hasAttemptedSubmit))
        if (showErrors || hasAttemptedSubmit) {
            setError(error)
        } else if (error == null) {
            setError(null)
        }
        loginButton.isEnabled = error == null && !isLoading
        updateButtonState()
        return error
    }

    private fun setError(message: String?) {
        if (message.isNullOrBlank()) {
            errorText.text = ""
            errorText.visibility = View.GONE
        } else {
            errorText.text = message
            errorText.visibility = View.VISIBLE
        }
    }

    private fun updateFieldState(container: View, hasError: Boolean) {
        container.setBackgroundResource(
            if (hasError) R.drawable.bg_text_field_error else R.drawable.bg_text_field
        )
    }

    private fun updateButtonState() {
        loginButton.setBackgroundResource(
            if (loginButton.isEnabled) R.drawable.bg_btn_primary else R.drawable.bg_btn_primary_disabled
        )
        loginButton.alpha = if (loginButton.isEnabled) 1f else 0.7f
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

    private fun enforceCursorAtEndOnFocus(input: EditText) {
        input.setOnFocusChangeListener { _, hasFocus ->
            if (hasFocus) {
                input.post {
                    val len = input.text?.length ?: 0
                    input.setSelection(len, len)
                }
            }
        }
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

    private fun tr(en: String, zh: String): String = if (isChineseLanguage(appConfig.getAppLanguage())) zh else en

    private fun isChineseLanguage(languageCode: String?): Boolean {
        if (languageCode.isNullOrBlank()) return false
        return languageCode.lowercase(Locale.US).startsWith("zh")
    }

    companion object {
        const val EXTRA_FORCE_REAUTH = "force_reauth"
    }
}
