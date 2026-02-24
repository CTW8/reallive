package com.reallive.android.ui.watch

import android.content.Intent
import android.os.Bundle
import android.content.ClipData
import android.content.ClipboardManager
import android.widget.TextView
import android.widget.Toast
import android.widget.CheckBox
import android.widget.RadioGroup
import androidx.appcompat.app.AppCompatActivity
import androidx.lifecycle.lifecycleScope
import com.reallive.android.R
import com.reallive.android.config.AppConfig
import com.reallive.android.data.CameraRepository
import com.reallive.android.network.ApiFactory
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.util.Locale

class ShareSheetActivity : AppCompatActivity() {
    private var currentLink: String = ""
    private var cameraId: Long = -1L
    private var zh: Boolean = false
    private lateinit var appConfig: AppConfig
    private lateinit var repository: CameraRepository
    private lateinit var ttlGroup: RadioGroup
    private lateinit var oneTimeCheck: CheckBox

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_share_sheet)
        appConfig = AppConfig(this)
        zh = isChineseLanguage(appConfig.getAppLanguage())
        repository = CameraRepository(ApiFactory.createAuthorized(appConfig))
        val streamKey = intent.getStringExtra(EXTRA_STREAM_KEY).orEmpty()
        cameraId = intent.getLongExtra(EXTRA_CAMERA_ID, -1L)
        val baseUrl = appConfig.getBaseUrl()
        currentLink = if (streamKey.isNotBlank()) {
            "${baseUrl}live/$streamKey.flv"
        } else {
            baseUrl
        }
        applyLocalizedTexts(zh)
        val linkInput = findViewById<TextView>(R.id.share_link_input)
        ttlGroup = findViewById(R.id.share_ttl_group)
        oneTimeCheck = findViewById(R.id.share_one_time)
        findViewById<android.widget.RadioButton>(R.id.share_ttl_7d).isChecked = true
        linkInput.text = currentLink

        if (streamKey.isBlank() && cameraId > 0L) {
            lifecycleScope.launch {
                runCatching {
                    withContext(Dispatchers.IO) { repository.getStreamInfo(cameraId) }
                }.onSuccess { stream ->
                    val key = stream.stream_key.takeIf { it.isNotBlank() }
                    if (key != null) {
                        currentLink = "${baseUrl}live/$key.flv"
                        linkInput.text = currentLink
                    }
                }
            }
        }

        if (cameraId > 0L) requestShareLink(mode = "view", autoShare = false)

        findViewById<android.view.View>(R.id.share_back).setOnClickListener { finish() }
        findViewById<android.view.View>(R.id.share_scrim).setOnClickListener { finish() }
        findViewById<android.view.View>(R.id.share_copy).setOnClickListener {
            val clipboard = getSystemService(ClipboardManager::class.java)
            clipboard.setPrimaryClip(
                ClipData.newPlainText(
                    if (zh) "RealLive 链接" else "RealLive link",
                    currentLink,
                ),
            )
            Toast.makeText(this, if (zh) "链接已复制" else "Link copied", Toast.LENGTH_SHORT).show()
        }
        findViewById<android.view.View>(R.id.share_icon_wechat).setOnClickListener { requestShareLink("view", true) }
        findViewById<android.view.View>(R.id.share_icon_telegram).setOnClickListener { requestShareLink("view", true) }
        findViewById<android.view.View>(R.id.share_icon_email).setOnClickListener { requestShareLink("view", true) }
        findViewById<android.view.View>(R.id.share_icon_sms).setOnClickListener { requestShareLink("view", true) }
        findViewById<android.view.View>(R.id.share_icon_more).setOnClickListener { requestShareLink("view", true) }
        findViewById<android.view.View>(R.id.share_system).setOnClickListener {
            requestShareLink("view", true)
        }
        findViewById<android.view.View>(R.id.share_view_24h).setOnClickListener {
            requestShareLink("24h", true)
        }
    }

    private fun requestShareLink(mode: String, autoShare: Boolean) {
        val linkInput = findViewById<TextView>(R.id.share_link_input)
        if (cameraId <= 0L) {
            if (autoShare) shareViaSystem(currentLink)
            return
        }
        lifecycleScope.launch {
            val ttlSec = selectedTtlSec(mode)
            val oneTime = oneTimeCheck.isChecked
            runCatching {
                withContext(Dispatchers.IO) {
                    repository.createShareLink(
                        cameraId = cameraId,
                        mode = mode,
                        ttlSec = ttlSec,
                        oneTime = oneTime,
                    )
                }
            }
                .onSuccess { resp ->
                    if (resp.url.isNotBlank()) {
                        currentLink = resp.url
                        linkInput.text = currentLink
                    }
                    if (autoShare) shareViaSystem(currentLink)
                }
                .onFailure {
                    if (autoShare) shareViaSystem(currentLink)
                }
        }
    }

    private fun selectedTtlSec(mode: String): Int {
        if (mode == "24h") return 24 * 60 * 60
        return when (ttlGroup.checkedRadioButtonId) {
            R.id.share_ttl_1h -> 1 * 60 * 60
            R.id.share_ttl_24h -> 24 * 60 * 60
            R.id.share_ttl_30d -> 30 * 24 * 60 * 60
            else -> 7 * 24 * 60 * 60
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
        findViewById<TextView>(R.id.share_ttl_label).text = if (zh) "链接有效期" else "Link expiry"
        findViewById<android.widget.RadioButton>(R.id.share_ttl_1h).text = if (zh) "1小时" else "1h"
        findViewById<android.widget.RadioButton>(R.id.share_ttl_24h).text = if (zh) "24小时" else "24h"
        findViewById<android.widget.RadioButton>(R.id.share_ttl_7d).text = if (zh) "7天" else "7d"
        findViewById<android.widget.RadioButton>(R.id.share_ttl_30d).text = if (zh) "30天" else "30d"
        findViewById<CheckBox>(R.id.share_one_time).text = if (zh) "一次性链接" else "One-time link"
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
