package com.reallive.android.ui.watch

import android.content.Intent
import android.os.Bundle
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import com.google.android.material.button.MaterialButton
import com.google.android.material.textfield.TextInputEditText
import com.reallive.android.R

class ShareSheetActivity : AppCompatActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_share_sheet)

        val cameraName = intent.getStringExtra(EXTRA_CAMERA_NAME) ?: "Camera"
        val link = "https://reallive.local/share?camera=${cameraName}"
        val linkInput = findViewById<TextInputEditText>(R.id.share_link_input)
        linkInput.setText(link)

        findViewById<MaterialButton>(R.id.share_close).setOnClickListener { finish() }
        findViewById<MaterialButton>(R.id.share_copy).setOnClickListener {
            Toast.makeText(this, "Share link copied (mock)", Toast.LENGTH_SHORT).show()
        }
        findViewById<MaterialButton>(R.id.share_system).setOnClickListener {
            val intent = Intent(Intent.ACTION_SEND).apply {
                type = "text/plain"
                putExtra(Intent.EXTRA_SUBJECT, "RealLive camera share")
                putExtra(Intent.EXTRA_TEXT, linkInput.text?.toString().orEmpty())
            }
            startActivity(Intent.createChooser(intent, getString(R.string.watch_action_share)))
        }
    }

    companion object {
        const val EXTRA_CAMERA_NAME = "extra_camera_name"
    }
}
