package com.reallive.android.ui.auth

import android.content.Intent
import android.os.Bundle
import android.view.View
import android.view.inputmethod.EditorInfo
import android.widget.Button
import android.widget.EditText
import android.widget.TextView
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import com.reallive.android.R
import com.reallive.android.config.AppConfig
import com.reallive.android.ui.dashboard.DashboardActivity

class LoginActivity : AppCompatActivity() {
    private lateinit var appConfig: AppConfig
    private lateinit var usernameInput: EditText
    private lateinit var passwordInput: EditText
    private lateinit var errorText: TextView
    private lateinit var loginButton: Button
    private lateinit var goRegisterButton: TextView
    private lateinit var forgotPasswordButton: TextView
    private lateinit var googleLoginButton: Button
    private lateinit var biometricLoginButton: Button
    private lateinit var loadingView: View

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_login)

        appConfig = AppConfig(this)
        if (!appConfig.getToken().isNullOrBlank()) {
            startActivity(Intent(this, DashboardActivity::class.java))
            finish()
            return
        }

        usernameInput = findViewById(R.id.input_username)
        passwordInput = findViewById(R.id.input_password)
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
        goRegisterButton.setOnClickListener {
            startActivity(Intent(this, RegisterActivity::class.java))
        }
        forgotPasswordButton.setOnClickListener {
            startActivity(Intent(this, ForgotPasswordActivity::class.java))
        }
        googleLoginButton.setOnClickListener {
            Toast.makeText(this, "Google sign-in (mock)", Toast.LENGTH_SHORT).show()
        }
        biometricLoginButton.setOnClickListener {
            Toast.makeText(this, "Biometric sign-in (mock)", Toast.LENGTH_SHORT).show()
        }
    }

    private fun login() {
        val username = usernameInput.text.toString().trim()
        val password = passwordInput.text.toString()

        if (username.isBlank() || password.isBlank()) {
            errorText.text = "Email and password are required."
            return
        }

        setLoading(true)
        errorText.text = ""
        appConfig.setToken("prototype-token")
        appConfig.setUserId(1L)
        appConfig.setUsername(username.ifBlank { "Alex" })
        setLoading(false)
        startActivity(Intent(this@LoginActivity, DashboardActivity::class.java))
        finish()
    }

    private fun setLoading(loading: Boolean) {
        loginButton.isEnabled = !loading
        goRegisterButton.isEnabled = !loading
        forgotPasswordButton.isEnabled = !loading
        googleLoginButton.isEnabled = !loading
        biometricLoginButton.isEnabled = !loading
        loadingView.visibility = if (loading) View.VISIBLE else View.GONE
    }
}
