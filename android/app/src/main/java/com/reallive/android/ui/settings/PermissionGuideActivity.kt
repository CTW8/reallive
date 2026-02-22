package com.reallive.android.ui.settings

import android.Manifest
import android.os.Bundle
import android.os.Build
import android.content.pm.PackageManager
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import androidx.activity.result.contract.ActivityResultContracts
import androidx.core.content.ContextCompat
import com.reallive.android.R

class PermissionGuideActivity : AppCompatActivity() {
    private val permissionLauncher = registerForActivityResult(
        ActivityResultContracts.RequestMultiplePermissions(),
    ) {
        refreshBadges()
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_permission_guide)
        findViewById<android.view.View>(R.id.permission_back).setOnClickListener { finish() }
        findViewById<android.view.View>(R.id.permission_later_btn).setOnClickListener { finish() }
        findViewById<android.view.View>(R.id.permission_continue_btn).setOnClickListener {
            requestMissingPermissions()
        }
    }

    override fun onStart() {
        super.onStart()
        refreshBadges()
    }

    private fun requestMissingPermissions() {
        val pending = mutableListOf<String>()
        if (!isNotificationGranted() && Build.VERSION.SDK_INT >= 33) {
            pending += Manifest.permission.POST_NOTIFICATIONS
        }
        if (!isPermissionGranted(Manifest.permission.RECORD_AUDIO)) {
            pending += Manifest.permission.RECORD_AUDIO
        }
        if (Build.VERSION.SDK_INT >= 33) {
            if (!isPermissionGranted(Manifest.permission.READ_MEDIA_IMAGES)) {
                pending += Manifest.permission.READ_MEDIA_IMAGES
            }
            if (!isPermissionGranted(Manifest.permission.READ_MEDIA_VIDEO)) {
                pending += Manifest.permission.READ_MEDIA_VIDEO
            }
        } else {
            if (!isPermissionGranted(Manifest.permission.READ_EXTERNAL_STORAGE)) {
                pending += Manifest.permission.READ_EXTERNAL_STORAGE
            }
        }
        if (!isPermissionGranted(Manifest.permission.ACCESS_FINE_LOCATION)) {
            pending += Manifest.permission.ACCESS_FINE_LOCATION
        }
        if (pending.isNotEmpty()) {
            permissionLauncher.launch(pending.toTypedArray())
        }
    }

    private fun refreshBadges() {
        setBadge(
            textView = findViewById(R.id.permission_badge_notifications),
            enabled = isNotificationGranted(),
            optional = false,
        )
        setBadge(
            textView = findViewById(R.id.permission_badge_mic),
            enabled = isPermissionGranted(Manifest.permission.RECORD_AUDIO),
            optional = false,
        )
        setBadge(
            textView = findViewById(R.id.permission_badge_media),
            enabled = isMediaGranted(),
            optional = false,
        )
        setBadge(
            textView = findViewById(R.id.permission_badge_location),
            enabled = isPermissionGranted(Manifest.permission.ACCESS_FINE_LOCATION),
            optional = true,
        )
    }

    private fun setBadge(textView: TextView, enabled: Boolean, optional: Boolean) {
        if (enabled) {
            textView.text = "Enabled"
            textView.setBackgroundResource(R.drawable.bg_perm_badge_on)
            textView.setTextColor(0xFF7DD881.toInt())
            return
        }
        if (optional) {
            textView.text = "Optional"
            textView.setBackgroundResource(R.drawable.bg_perm_badge_neutral)
            textView.setTextColor(ContextCompat.getColor(this, R.color.auth_on_surface_variant))
            return
        }
        textView.text = "Needed"
        textView.setBackgroundResource(R.drawable.bg_perm_badge_off)
        textView.setTextColor(ContextCompat.getColor(this, R.color.auth_on_surface_variant))
    }

    private fun isNotificationGranted(): Boolean {
        if (Build.VERSION.SDK_INT < 33) return true
        return isPermissionGranted(Manifest.permission.POST_NOTIFICATIONS)
    }

    private fun isMediaGranted(): Boolean {
        return if (Build.VERSION.SDK_INT >= 33) {
            isPermissionGranted(Manifest.permission.READ_MEDIA_IMAGES) &&
                isPermissionGranted(Manifest.permission.READ_MEDIA_VIDEO)
        } else {
            isPermissionGranted(Manifest.permission.READ_EXTERNAL_STORAGE)
        }
    }

    private fun isPermissionGranted(permission: String): Boolean {
        return ContextCompat.checkSelfPermission(this, permission) == PackageManager.PERMISSION_GRANTED
    }
}
