package com.reallive.android.ui.camera

import android.os.Bundle
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import com.google.android.material.appbar.MaterialToolbar
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import com.reallive.android.R

class CameraSettingsActivity : AppCompatActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_camera_settings)

        val cameraName = intent.getStringExtra(EXTRA_CAMERA_NAME) ?: "Camera"
        findViewById<MaterialToolbar>(R.id.camera_settings_toolbar).apply {
            subtitle = cameraName
            setNavigationOnClickListener { finish() }
        }
        findViewById<TextView>(R.id.camera_settings_name).text = cameraName
        findViewById<android.view.View>(R.id.camera_settings_remove).setOnClickListener {
            MaterialAlertDialogBuilder(this)
                .setTitle("Remove Camera?")
                .setMessage("This action is mock-only in prototype mode.")
                .setNegativeButton("Cancel", null)
                .setPositiveButton("Remove", null)
                .show()
        }
    }

    companion object {
        const val EXTRA_CAMERA_ID = "extra_camera_id"
        const val EXTRA_CAMERA_NAME = "extra_camera_name"
    }
}
