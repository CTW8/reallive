package com.reallive.android.ui.camera

import android.content.Intent
import android.os.Bundle
import android.view.View
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import com.reallive.android.R
import com.reallive.android.config.AppConfig
import java.util.Locale

class AddCameraActivity : AppCompatActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val appConfig = AppConfig(this)
        if (appConfig.getToken().isNullOrBlank()) {
            finish()
            return
        }

        setContentView(R.layout.activity_add_camera)
        applyLocalizedTexts(isChineseLanguage(appConfig.getAppLanguage()))
        findViewById<android.view.View>(R.id.add_camera_close).setOnClickListener { finish() }

        findViewById<View>(R.id.add_camera_scan_btn).setOnClickListener {
            startActivity(Intent(this, CameraSetupActivity::class.java))
        }
        findViewById<View>(R.id.add_camera_nearby_btn).setOnClickListener {
            startActivity(Intent(this, CameraSetupActivity::class.java))
        }
    }

    private fun applyLocalizedTexts(zh: Boolean) {
        findViewById<TextView>(R.id.add_camera_page_title).text = if (zh) "添加摄像头" else "Add Camera"
        findViewById<TextView>(R.id.add_camera_scan_hint).text =
            if (zh) "将摄像头二维码对准框内" else "Align QR code from camera within the frame"
        findViewById<TextView>(R.id.add_camera_sheet_hint).text =
            if (zh) "扫描摄像头设备或包装上的二维码" else "Scan the QR code on your camera device or its packaging"
        findViewById<TextView>(R.id.add_camera_scan_btn).text =
            if (zh) "手动输入编码" else "Enter code manually"
        findViewById<TextView>(R.id.add_camera_nearby_btn).text =
            if (zh) "搜索附近设备" else "Search nearby devices"
    }

    private fun isChineseLanguage(languageCode: String?): Boolean {
        if (languageCode.isNullOrBlank()) return false
        return languageCode.lowercase(Locale.US).startsWith("zh")
    }
}
