package com.reallive.android.ui.auth

import android.os.Bundle
import android.widget.TextView
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import com.google.android.material.appbar.MaterialToolbar
import com.google.android.material.button.MaterialButton
import com.google.android.material.textfield.TextInputEditText
import com.reallive.android.R

class ForgotPasswordActivity : AppCompatActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_forgot_password)

        findViewById<MaterialToolbar>(R.id.forgot_toolbar).setNavigationOnClickListener { finish() }

        val emailInput = findViewById<TextInputEditText>(R.id.forgot_email)
        val codeInput = findViewById<TextInputEditText>(R.id.forgot_code)
        val passInput = findViewById<TextInputEditText>(R.id.forgot_new_password)
        val confirmInput = findViewById<TextInputEditText>(R.id.forgot_confirm_password)
        val statusText = findViewById<TextView>(R.id.forgot_status)

        findViewById<MaterialButton>(R.id.forgot_send_code).setOnClickListener {
            val email = emailInput.text?.toString()?.trim().orEmpty()
            statusText.text = if (email.isBlank()) {
                getString(R.string.error_email_required)
            } else {
                getString(R.string.forgot_sent_mock)
            }
        }

        findViewById<MaterialButton>(R.id.forgot_submit).setOnClickListener {
            val code = codeInput.text?.toString()?.trim().orEmpty()
            val pass = passInput.text?.toString().orEmpty()
            val confirm = confirmInput.text?.toString().orEmpty()
            val message = when {
                code.length != 6 -> getString(R.string.forgot_code_required)
                pass.length < 8 -> getString(R.string.register_password_min_error)
                pass != confirm -> getString(R.string.forgot_password_mismatch)
                else -> getString(R.string.forgot_reset_success)
            }
            statusText.text = message
            if (message == getString(R.string.forgot_reset_success)) {
                Toast.makeText(this, getString(R.string.forgot_reset_toast), Toast.LENGTH_SHORT).show()
                finish()
            }
        }
    }
}
