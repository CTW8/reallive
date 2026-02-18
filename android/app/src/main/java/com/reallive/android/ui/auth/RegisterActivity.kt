package com.reallive.android.ui.auth

import android.content.Intent
import android.os.Bundle
import android.view.View
import android.view.inputmethod.EditorInfo
import android.widget.CheckBox
import android.widget.Button
import android.widget.EditText
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import com.google.android.material.appbar.MaterialToolbar
import com.reallive.android.R
import com.reallive.android.config.AppConfig
import com.reallive.android.ui.dashboard.DashboardActivity

class RegisterActivity : AppCompatActivity() {
    private lateinit var appConfig: AppConfig
    private lateinit var usernameInput: EditText
    private lateinit var emailInput: EditText
    private lateinit var phoneInput: EditText
    private lateinit var passwordInput: EditText
    private lateinit var confirmPasswordInput: EditText
    private lateinit var termsCheckbox: CheckBox
    private lateinit var errorText: TextView
    private lateinit var registerButton: Button
    private lateinit var loadingView: View

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_register)

        appConfig = AppConfig(this)
        if (!appConfig.getToken().isNullOrBlank()) {
            startActivity(Intent(this, DashboardActivity::class.java))
            finish()
            return
        }

        usernameInput = findViewById(R.id.register_input_username)
        emailInput = findViewById(R.id.register_input_email)
        phoneInput = findViewById(R.id.register_input_phone)
        passwordInput = findViewById(R.id.register_input_password)
        confirmPasswordInput = findViewById(R.id.register_input_confirm_password)
        termsCheckbox = findViewById(R.id.register_terms_checkbox)
        errorText = findViewById(R.id.register_error)
        registerButton = findViewById(R.id.btn_register)
        loadingView = findViewById(R.id.register_loading)
        findViewById<MaterialToolbar>(R.id.register_toolbar).setNavigationOnClickListener { finish() }

        registerButton.setOnClickListener { register() }
        confirmPasswordInput.setOnEditorActionListener { _, actionId, _ ->
            if (actionId == EditorInfo.IME_ACTION_DONE) {
                register()
                true
            } else {
                false
            }
        }
    }

    private fun register() {
        val username = usernameInput.text.toString().trim()
        val email = emailInput.text.toString().trim()
        val phone = phoneInput.text.toString().trim()
        val password = passwordInput.text.toString()
        val confirm = confirmPasswordInput.text.toString()

        if (username.isBlank() || email.isBlank() || phone.isBlank() || password.isBlank()) {
            errorText.text = "All fields are required."
            return
        }
        if (password.length < 8) {
            errorText.text = "Password must be at least 8 characters."
            return
        }
        if (password != confirm) {
            errorText.text = "Passwords do not match."
            return
        }
        if (!termsCheckbox.isChecked) {
            errorText.text = "Please agree to the terms."
            return
        }

        setLoading(true)
        errorText.text = ""
        appConfig.setToken("prototype-token")
        appConfig.setUserId(1L)
        appConfig.setUsername(username)
        setLoading(false)
        startActivity(Intent(this@RegisterActivity, DashboardActivity::class.java))
        finish()
    }

    private fun setLoading(loading: Boolean) {
        registerButton.isEnabled = !loading
        termsCheckbox.isEnabled = !loading
        loadingView.visibility = if (loading) View.VISIBLE else View.GONE
    }
}
