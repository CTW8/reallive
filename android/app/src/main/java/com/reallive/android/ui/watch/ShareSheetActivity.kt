package com.reallive.android.ui.watch

import android.content.Intent
import android.os.Bundle
import android.content.ClipData
import android.content.ClipboardManager
import android.widget.TextView
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import androidx.lifecycle.lifecycleScope
import com.reallive.android.R
import com.reallive.android.config.AppConfig
import com.reallive.android.data.CameraRepository
import com.reallive.android.network.ApiClient
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.util.Locale

class ShareSheetActivity : AppCompatActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_share_sheet)
        val appConfig = AppConfig(this)
        val zh = isChineseLanguage(appConfig.getAppLanguage())

        val streamKey = intent.getStringExtra(EXTRA_STREAM_KEY).orEmpty()
        val cameraId = intent.getLongExtra(EXTRA_CAMERA_ID, -1L)
        val baseUrl = appConfig.getBaseUrl()
        var link = if (streamKey.isNotBlank()) {
            "${baseUrl}live/$streamKey.flv"
        } else {
            baseUrl
        }
        applyLocalizedTexts(zh)
        val linkInput = findViewById<TextView>(R.id.share_link_input)
        linkInput.text = link

        if (streamKey.isBlank() && cameraId > 0L) {
            val repository = CameraRepository(ApiClient.create(baseUrl) { appConfig.getToken() })
            lifecycleScope.launch {
                runCatching {
                    withContext(Dispatchers.IO) { repository.getStreamInfo(cameraId) }
                }.onSuccess { stream ->
                    val key = stream.stream_key.takeIf { it.isNotBlank() }
                    if (key != null) {
                        link = "${baseUrl}live/$key.flv"
                        linkInput.text = link
                    }
                }
            }
        }

        findViewById<android.view.View>(R.id.share_back).setOnClickListener { finish() }
        findViewById<android.view.View>(R.id.share_scrim).setOnClickListener { finish() }
        findViewById<android.view.View>(R.id.share_copy).setOnClickListener {
            val clipboard = getSystemService(ClipboardManager::class.java)
            clipboard.setPrimaryClip(
                ClipData.newPlainText(
                    if (zh) "RealLive 链接" else "RealLive link",
                    linkInput.text.toString(),
                ),
            )
            Toast.makeText(this, if (zh) "链接已复制" else "Link copied", Toast.LENGTH_SHORT).show()
        }
        findViewById<android.view.View>(R.id.share_icon_wechat).setOnClickListener { shareViaSystem(linkInput.text.toString()) }
        findViewById<android.view.View>(R.id.share_icon_telegram).setOnClickListener { shareViaSystem(linkInput.text.toString()) }
        findViewById<android.view.View>(R.id.share_icon_email).setOnClickListener { shareViaSystem(linkInput.text.toString()) }
        findViewById<android.view.View>(R.id.share_icon_sms).setOnClickListener { shareViaSystem(linkInput.text.toString()) }
        findViewById<android.view.View>(R.id.share_icon_more).setOnClickListener { shareViaSystem(linkInput.text.toString()) }
        findViewById<android.view.View>(R.id.share_system).setOnClickListener {
            shareViaSystem(linkInput.text.toString())
        }
        findViewById<android.view.View>(R.id.share_view_24h).setOnClickListener {
            shareViaSystem(linkInput.text.toString())
        }
    }

    private fun shareViaSystem(content: String) {
        val intent = Intent(Intent.ACTION_SEND).apply {
            type = "text/plain"
            putExtra(Intent.EXTRA_SUBJECT, "RealLive camera share")
            putExtra(Intent.EXTRA_TEXT, content)
        }
        startActivity(Intent.createChooser(intent, getString(R.string.watch_action_share)))
    }

    private fun applyLocalizedTexts(zh: Boolean) {
        findViewById<TextView>(R.id.share_topbar_title).text = if (zh) "分享摄像头" else "Share Camera"
        findViewById<TextView>(R.id.share_sheet_title).text = if (zh) "分享摄像头" else "Share Camera"
        findViewById<TextView>(R.id.share_label_wechat).text = if (zh) "微信" else "WeChat"
        findViewById<TextView>(R.id.share_label_telegram).text = "Telegram"
        findViewById<TextView>(R.id.share_label_email).text = if (zh) "邮件" else "Email"
        findViewById<TextView>(R.id.share_label_sms).text = "SMS"
        findViewById<TextView>(R.id.share_label_more).text = if (zh) "更多" else "More"
        findViewById<TextView>(R.id.share_link_title).text = if (zh) "或通过链接分享" else "Or share via link"
        findViewById<TextView>(R.id.share_system).text = if (zh) "仅观看" else "View Only"
        findViewById<TextView>(R.id.share_view_24h).text = if (zh) "24小时链接" else "24h Link"
    }

    private fun isChineseLanguage(languageCode: String?): Boolean {
        if (languageCode.isNullOrBlank()) return false
        return languageCode.lowercase(Locale.US).startsWith("zh")
    }

    companion object {
        const val EXTRA_CAMERA_NAME = "extra_camera_name"
        const val EXTRA_STREAM_KEY = "extra_stream_key"
        const val EXTRA_CAMERA_ID = "extra_camera_id"
    }
}
