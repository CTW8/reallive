package com.reallive.android.ui.camera

import android.content.Intent
import android.os.Bundle
import android.widget.ArrayAdapter
import android.widget.AutoCompleteTextView
import android.widget.TextView
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import com.google.android.material.appbar.MaterialToolbar
import com.google.android.material.button.MaterialButton
import com.google.android.material.textfield.TextInputEditText
import com.reallive.android.R
import com.reallive.android.config.AppConfig
import java.util.UUID

class AddCameraActivity : AppCompatActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val appConfig = AppConfig(this)
        if (appConfig.getToken().isNullOrBlank()) {
            finish()
            return
        }

        setContentView(R.layout.activity_add_camera)
        findViewById<MaterialToolbar>(R.id.add_camera_toolbar).setNavigationOnClickListener { finish() }

        val inputName = findViewById<TextInputEditText>(R.id.add_camera_name)
        val inputResolution = findViewById<AutoCompleteTextView>(R.id.add_camera_resolution)
        val inputScanKey = findViewById<TextInputEditText>(R.id.add_camera_scan_key)
        val errorText = findViewById<TextView>(R.id.add_camera_error)

        val options = listOf("720p", "1080p", "1440p", "4K")
        inputResolution.setAdapter(ArrayAdapter(this, android.R.layout.simple_list_item_1, options))
        inputResolution.setText("1080p", false)

        findViewById<MaterialButton>(R.id.add_camera_scan_btn).setOnClickListener {
            inputScanKey.setText(UUID.randomUUID().toString().take(12))
            if (inputName.text.isNullOrBlank()) {
                inputName.setText("Backyard Camera")
            }
            Toast.makeText(this, "QR detected (mock)", Toast.LENGTH_SHORT).show()
        }
        findViewById<MaterialButton>(R.id.add_camera_nearby_btn).setOnClickListener {
            inputName.setText("Backyard Camera")
            inputScanKey.setText("RL-2026-A8F3")
            Toast.makeText(this, "Found 1 nearby device (mock)", Toast.LENGTH_SHORT).show()
        }

        findViewById<MaterialButton>(R.id.add_camera_submit).setOnClickListener {
            val name = inputName.text?.toString()?.trim().orEmpty()
            if (name.isBlank()) {
                errorText.text = "Camera name is required"
                return@setOnClickListener
            }

            errorText.text = ""
            startActivity(Intent(this, CameraSetupActivity::class.java))
        }
    }
}
