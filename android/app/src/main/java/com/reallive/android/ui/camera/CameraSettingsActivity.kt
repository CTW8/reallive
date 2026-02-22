package com.reallive.android.ui.camera

import android.content.Intent
import android.os.Bundle
import android.view.Gravity
import android.view.View
import android.widget.EditText
import android.widget.FrameLayout
import android.widget.TextView
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import androidx.lifecycle.lifecycleScope
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import com.reallive.android.R
import com.reallive.android.config.AppConfig
import com.reallive.android.data.CameraRepository
import com.reallive.android.network.ApiClient
import com.reallive.android.network.CameraSettingsDetailDto
import com.reallive.android.network.CameraSettingsResponse
import com.reallive.android.ui.auth.LoginActivity
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import retrofit2.HttpException
import java.util.Locale

class CameraSettingsActivity : AppCompatActivity() {
    private lateinit var appConfig: AppConfig
    private lateinit var repository: CameraRepository
    private var cameraId: Long = -1L
    private var cameraName: String = "Camera"
    private var model: CameraSettingsResponse? = null
    private var saving = false

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_camera_settings)

        appConfig = AppConfig(this)
        if (appConfig.getToken().isNullOrBlank()) {
            finish()
            return
        }
        repository = CameraRepository(ApiClient.create(appConfig.getBaseUrl(), appConfig::getToken))
        cameraName = intent.getStringExtra(EXTRA_CAMERA_NAME) ?: if (zh()) "摄像头" else "Camera"
        cameraId = intent.getLongExtra(EXTRA_CAMERA_ID, -1L)
        if (cameraId <= 0L) {
            Toast.makeText(this, tr("Invalid camera", "无效摄像头"), Toast.LENGTH_SHORT).show()
            finish()
            return
        }

        findViewById<View>(R.id.camera_settings_back).setOnClickListener { finish() }
        applyLocalizedTexts()
        findViewById<TextView>(R.id.camera_settings_name).text = cameraName

        findViewById<View>(R.id.camera_settings_row_name).setOnClickListener { editName() }
        findViewById<View>(R.id.camera_settings_row_location).setOnClickListener { editLocation() }
        findViewById<View>(R.id.camera_settings_row_resolution).setOnClickListener { pickResolution() }
        findViewById<View>(R.id.camera_settings_row_motion).setOnClickListener { pickMotionSensitivity() }
        findViewById<View>(R.id.camera_settings_row_person).setOnClickListener { pickPersonDetection() }
        findViewById<View>(R.id.camera_settings_row_sound).setOnClickListener { pickSoundSensitivity() }
        findViewById<View>(R.id.camera_settings_row_night).setOnClickListener { pickNightMode() }
        findViewById<View>(R.id.camera_settings_row_zones).setOnClickListener { pickZonePreset() }
        findViewById<View>(R.id.camera_settings_row_flip).setOnClickListener { pickImageFlip() }
        findViewById<View>(R.id.camera_settings_row_watermark).setOnClickListener {
            toggleSetting { copy(watermark_enabled = !watermark_enabled) }
        }
        findViewById<View>(R.id.camera_settings_row_wifi).setOnClickListener { showWifiDialog() }
        findViewById<View>(R.id.camera_settings_row_firmware).setOnClickListener { triggerFirmwareUpdate() }
        findViewById<View>(R.id.camera_settings_firmware_action).setOnClickListener { triggerFirmwareUpdate() }

        findViewById<View>(R.id.camera_settings_motion_switch).setOnClickListener {
            toggleSetting { copy(motion_enabled = !motion_enabled) }
        }
        findViewById<View>(R.id.camera_settings_person_switch).setOnClickListener {
            toggleSetting { copy(person_enabled = !person_enabled) }
        }
        findViewById<View>(R.id.camera_settings_sound_switch).setOnClickListener {
            toggleSetting { copy(sound_enabled = !sound_enabled) }
        }
        findViewById<View>(R.id.camera_settings_night_switch).setOnClickListener {
            toggleSetting { copy(night_vision_enabled = !night_vision_enabled) }
        }
        findViewById<View>(R.id.camera_settings_watermark_switch).setOnClickListener {
            toggleSetting { copy(watermark_enabled = !watermark_enabled) }
        }

        findViewById<View>(R.id.camera_settings_remove).setOnClickListener { confirmRemove() }
    }

    override fun onStart() {
        super.onStart()
        loadSettings()
        refreshNetworkSummary()
    }

    private fun loadSettings() {
        if (cameraId <= 0L) return
        lifecycleScope.launch {
            try {
                val result = withContext(Dispatchers.IO) { repository.getCameraSettings(cameraId) }
                model = result
                render(result)
            } catch (ex: Exception) {
                if (!handleAuth(ex)) {
                    Toast.makeText(this@CameraSettingsActivity, tr("Failed to load camera settings", "加载摄像头设置失败"), Toast.LENGTH_SHORT).show()
                }
            }
        }
    }

    private fun render(data: CameraSettingsResponse) {
        cameraName = data.name.ifBlank { cameraName }
        findViewById<TextView>(R.id.camera_settings_name).text = cameraName
        findViewById<TextView>(R.id.camera_settings_name_value).text = tr("Camera Name", "摄像头名称")
        findViewById<TextView>(R.id.camera_settings_name_subtitle).text = cameraName
        findViewById<TextView>(R.id.camera_settings_motion_title).text = tr("Motion Detection", "移动检测")
        findViewById<TextView>(R.id.camera_settings_location_value).text = tr("Location", "位置")
        findViewById<TextView>(R.id.camera_settings_location_subtitle).text = data.location.ifBlank { tr("Not set", "未设置") }
        findViewById<TextView>(R.id.camera_settings_resolution_value).text = tr("Resolution", "分辨率")
        findViewById<TextView>(R.id.camera_settings_resolution_subtitle).text = resolutionLabel(data.resolution)
        findViewById<TextView>(R.id.camera_settings_motion_value).text =
            "${tr("Sensitivity", "灵敏度")}: ${motionSensitivityLabel(data.settings.motion_sensitivity)}"
        findViewById<TextView>(R.id.camera_settings_person_title).text = tr("Person Detection", "人物检测")
        findViewById<TextView>(R.id.camera_settings_person_value).text =
            if (data.settings.person_enabled) tr("AI-powered alerts", "AI 告警") else tr("Disabled", "已关闭")
        findViewById<TextView>(R.id.camera_settings_sound_title).text = tr("Sound Detection", "声音检测")
        findViewById<TextView>(R.id.camera_settings_sound_value).text =
            if (data.settings.sound_enabled) "${tr("Sensitivity", "灵敏度")}: ${soundSensitivityLabel(data.settings.sound_sensitivity)}" else tr("Disabled", "已关闭")
        findViewById<TextView>(R.id.camera_settings_zone_value).text = tr("Detection Zones", "检测区域")
        findViewById<TextView>(R.id.camera_settings_zone_subtitle).text = detectionZoneLabel(data.settings.detection_zones)
        findViewById<TextView>(R.id.camera_settings_night_title).text = tr("Night Vision", "夜视")
        findViewById<TextView>(R.id.camera_settings_night_value).text =
            if (data.settings.night_vision_enabled) nightModeLabel(data.settings.night_vision_mode) else tr("Disabled", "已关闭")
        findViewById<TextView>(R.id.camera_settings_flip_value).text = tr("Image Flip", "图像翻转")
        findViewById<TextView>(R.id.camera_settings_flip_subtitle).text = imageFlipLabel(data.settings.image_flip_mode)
        findViewById<TextView>(R.id.camera_settings_watermark_title).text = tr("Watermark", "水印")
        findViewById<TextView>(R.id.camera_settings_watermark_value).text =
            if (data.settings.watermark_enabled) tr("Timestamp enabled", "时间戳已启用") else tr("Disabled", "已关闭")
        findViewById<TextView>(R.id.camera_settings_firmware_title).text = tr("Firmware", "固件")
        findViewById<TextView>(R.id.camera_settings_firmware_value).text =
            "${data.settings.firmware_version} · ${if (data.settings.firmware_update_available) tr("Update available", "可更新") else tr("Latest", "最新")}"
        findViewById<TextView>(R.id.camera_settings_firmware_action).apply {
            text = if (data.settings.firmware_update_available) tr("Update", "更新") else tr("Latest", "最新")
            alpha = if (data.settings.firmware_update_available) 1f else 0.5f
        }

        findViewById<TextView>(R.id.camera_settings_status_text).text =
            when (data.status.lowercase()) {
                "online", "streaming" -> tr("Online", "在线")
                else -> tr("Offline", "离线")
            }
        findViewById<TextView>(R.id.camera_settings_wifi_title).text = "Wi-Fi"

        bindSwitch(R.id.camera_settings_motion_switch, R.id.camera_settings_motion_knob, data.settings.motion_enabled)
        bindSwitch(R.id.camera_settings_person_switch, R.id.camera_settings_person_knob, data.settings.person_enabled)
        bindSwitch(R.id.camera_settings_sound_switch, R.id.camera_settings_sound_knob, data.settings.sound_enabled)
        bindSwitch(R.id.camera_settings_night_switch, R.id.camera_settings_night_knob, data.settings.night_vision_enabled)
        bindSwitch(R.id.camera_settings_watermark_switch, R.id.camera_settings_watermark_knob, data.settings.watermark_enabled)
    }

    private fun editName() {
        val current = model ?: return
        val input = EditText(this).apply {
            setText(current.name)
            setSelection(text?.length ?: 0)
        }
        MaterialAlertDialogBuilder(this)
            .setTitle(tr("Camera Name", "摄像头名称"))
            .setView(input)
            .setNegativeButton(tr("Cancel", "取消"), null)
            .setPositiveButton(tr("Save", "保存")) { _, _ ->
                val next = input.text?.toString()?.trim().orEmpty()
                if (next.isBlank()) {
                    Toast.makeText(this, tr("Name cannot be empty", "名称不能为空"), Toast.LENGTH_SHORT).show()
                    return@setPositiveButton
                }
                saveCamera(name = next)
            }
            .show()
    }

    private fun editLocation() {
        val current = model ?: return
        val input = EditText(this).apply {
            setText(current.location)
            setSelection(text?.length ?: 0)
        }
        MaterialAlertDialogBuilder(this)
            .setTitle(tr("Location", "位置"))
            .setView(input)
            .setNegativeButton(tr("Cancel", "取消"), null)
            .setPositiveButton(tr("Save", "保存")) { _, _ ->
                saveCamera(location = input.text?.toString()?.trim().orEmpty())
            }
            .show()
    }

    private fun pickResolution() {
        val current = model ?: return
        val items = arrayOf("720p", "1080p", "2K", "4K")
        val idx = items.indexOfFirst { it.equals(current.resolution, true) }.let { if (it < 0) 1 else it }
        MaterialAlertDialogBuilder(this)
            .setTitle(tr("Resolution", "分辨率"))
            .setSingleChoiceItems(items, idx) { dialog, which ->
                dialog.dismiss()
                saveCamera(resolution = items[which])
            }
            .setNegativeButton(tr("Cancel", "取消"), null)
            .show()
    }

    private fun pickZonePreset() {
        val current = model ?: return
        val values = arrayOf("1 zone configured", "2 zones configured", "3 zones configured", "Custom zones")
        val labels = values.map { detectionZoneLabel(it) }.toTypedArray()
        val idx = values.indexOf(current.settings.detection_zones).let { if (it < 0) 1 else it }
        MaterialAlertDialogBuilder(this)
            .setTitle(tr("Detection Zones", "检测区域"))
            .setSingleChoiceItems(labels, idx) { dialog, which ->
                dialog.dismiss()
                saveSettings(current.settings.copy(detection_zones = values[which]))
            }
            .setNegativeButton(tr("Cancel", "取消"), null)
            .show()
    }

    private fun pickMotionSensitivity() {
        val current = model ?: return
        val values = arrayOf("Low", "Medium", "High")
        val labels = values.map { motionSensitivityLabel(it) }.toTypedArray()
        val idx = values.indexOf(current.settings.motion_sensitivity).let { if (it < 0) 2 else it }
        MaterialAlertDialogBuilder(this)
            .setTitle(tr("Motion Sensitivity", "移动灵敏度"))
            .setSingleChoiceItems(labels, idx) { dialog, which ->
                dialog.dismiss()
                saveSettings(current.settings.copy(motion_sensitivity = values[which]))
            }
            .setNegativeButton(tr("Cancel", "取消"), null)
            .show()
    }

    private fun pickSoundSensitivity() {
        val current = model ?: return
        val values = arrayOf("Quiet", "Normal", "Loud")
        val labels = values.map { soundSensitivityLabel(it) }.toTypedArray()
        val idx = values.indexOf(current.settings.sound_sensitivity).let { if (it < 0) 2 else it }
        MaterialAlertDialogBuilder(this)
            .setTitle(tr("Sound Sensitivity", "声音灵敏度"))
            .setSingleChoiceItems(labels, idx) { dialog, which ->
                dialog.dismiss()
                saveSettings(current.settings.copy(sound_sensitivity = values[which], sound_enabled = true))
            }
            .setNegativeButton(tr("Cancel", "取消"), null)
            .show()
    }

    private fun pickPersonDetection() {
        val current = model ?: return
        val options = arrayOf(
            tr("Enabled (AI alerts)", "启用（AI 告警）"),
            tr("Disabled", "已关闭"),
        )
        val index = if (current.settings.person_enabled) 0 else 1
        MaterialAlertDialogBuilder(this)
            .setTitle(tr("Person Detection", "人物检测"))
            .setSingleChoiceItems(options, index) { dialog, which ->
                dialog.dismiss()
                saveSettings(current.settings.copy(person_enabled = which == 0))
            }
            .setNegativeButton(tr("Cancel", "取消"), null)
            .show()
    }

    private fun pickNightMode() {
        val current = model ?: return
        val values = arrayOf("Auto", "On", "Off")
        val labels = values.map { nightModeLabel(it) }.toTypedArray()
        val currentMode = when {
            !current.settings.night_vision_enabled -> "Off"
            else -> current.settings.night_vision_mode
        }
        val idx = values.indexOf(currentMode).let { if (it < 0) 0 else it }
        MaterialAlertDialogBuilder(this)
            .setTitle(tr("Night Vision Mode", "夜视模式"))
            .setSingleChoiceItems(labels, idx) { dialog, which ->
                dialog.dismiss()
                val selected = values[which]
                val next = if (selected == "Off") {
                    current.settings.copy(night_vision_enabled = false, night_vision_mode = "Auto")
                } else {
                    current.settings.copy(night_vision_enabled = true, night_vision_mode = selected)
                }
                saveSettings(next)
            }
            .setNegativeButton(tr("Cancel", "取消"), null)
            .show()
    }

    private fun pickImageFlip() {
        val current = model ?: return
        val values = arrayOf("Normal", "Flip Horizontal", "Flip Vertical", "Rotate 180")
        val labels = values.map { imageFlipLabel(it) }.toTypedArray()
        val idx = values.indexOf(current.settings.image_flip_mode).let { if (it < 0) 0 else it }
        MaterialAlertDialogBuilder(this)
            .setTitle(tr("Image Flip", "图像翻转"))
            .setSingleChoiceItems(labels, idx) { dialog, which ->
                dialog.dismiss()
                saveSettings(current.settings.copy(image_flip_mode = values[which]))
            }
            .setNegativeButton(tr("Cancel", "取消"), null)
            .show()
    }

    private fun triggerFirmwareUpdate() {
        val current = model ?: return
        if (!current.settings.firmware_update_available) {
            Toast.makeText(this, tr("Already latest firmware", "已是最新固件"), Toast.LENGTH_SHORT).show()
            return
        }
        MaterialAlertDialogBuilder(this)
            .setTitle(tr("Firmware Update", "固件更新"))
            .setMessage(tr("Install latest firmware now?", "现在安装最新固件吗？"))
            .setNegativeButton(tr("Cancel", "取消"), null)
            .setPositiveButton(tr("Update", "更新")) { _, _ ->
                lifecycleScope.launch {
                    try {
                        val result = withContext(Dispatchers.IO) {
                            repository.triggerFirmwareUpdate(cameraId)
                        }
                        model?.let { existing ->
                            val next = existing.copy(
                                settings = existing.settings.copy(
                                    firmware_version = result.firmwareVersion ?: existing.settings.firmware_version,
                                    firmware_update_available = result.firmwareUpdateAvailable ?: false,
                                ),
                            )
                            model = next
                            render(next)
                        }
                        Toast.makeText(
                            this@CameraSettingsActivity,
                            tr("Firmware updated", "固件已更新"),
                            Toast.LENGTH_SHORT,
                        ).show()
                    } catch (ex: Exception) {
                        if (!handleAuth(ex)) {
                            Toast.makeText(this@CameraSettingsActivity, tr("Firmware update failed", "固件升级失败"), Toast.LENGTH_SHORT).show()
                        }
                    }
                }
            }
            .show()
    }

    private fun showWifiDialog() {
        lifecycleScope.launch {
            try {
                val info = withContext(Dispatchers.IO) { repository.getCameraNetworkInfo(cameraId) }
                val ssid = info.ssid ?: tr("Unknown", "未知")
                val signal = info.signal ?: tr("Unknown", "未知")
                val ip = info.ip ?: "N/A"
                val modelText = info.model ?: "RealLive Cam"
                findViewById<TextView>(R.id.camera_settings_wifi_value).text =
                    if (info.connected) "$ssid · ${info.signal ?: tr("Good", "良好")}" else tr("Disconnected", "未连接")
                MaterialAlertDialogBuilder(this@CameraSettingsActivity)
                    .setTitle(tr("Network Info", "网络信息"))
                    .setMessage(
                        "${tr("SSID", "SSID")}: $ssid\n${tr("Signal", "信号")}: $signal\nIP: $ip\n${tr("Model", "型号")}: $modelText\n${tr("Status", "状态")}: ${
                            if (info.connected) tr("Connected", "已连接") else tr("Disconnected", "未连接")
                        }",
                    )
                    .setPositiveButton("OK", null)
                    .show()
            } catch (ex: Exception) {
                if (!handleAuth(ex)) {
                    Toast.makeText(this@CameraSettingsActivity, tr("Failed to load network info", "网络信息加载失败"), Toast.LENGTH_SHORT).show()
                }
            }
        }
    }

    private fun refreshNetworkSummary() {
        if (cameraId <= 0L) return
        lifecycleScope.launch {
            try {
                val info = withContext(Dispatchers.IO) { repository.getCameraNetworkInfo(cameraId) }
                findViewById<TextView>(R.id.camera_settings_wifi_value).text =
                    if (info.connected) "${info.ssid ?: "Wi-Fi"} · ${info.signal ?: tr("Good", "良好")}" else tr("Disconnected", "未连接")
            } catch (_: Exception) {
            }
        }
    }

    private fun toggleSetting(transform: CameraSettingsDetailDto.() -> CameraSettingsDetailDto) {
        val current = model ?: return
        saveSettings(current.settings.transform())
    }

    private fun saveSettings(next: CameraSettingsDetailDto) {
        if (saving) return
        val current = model ?: return
        saving = true
        lifecycleScope.launch {
            try {
                val updated = withContext(Dispatchers.IO) {
                    repository.updateCameraSettings(cameraId = cameraId, settings = next)
                }
                model = updated
                render(updated)
                Toast.makeText(this@CameraSettingsActivity, tr("Settings saved", "设置已保存"), Toast.LENGTH_SHORT).show()
            } catch (ex: Exception) {
                if (!handleAuth(ex)) {
                    model = current
                    render(current)
                    Toast.makeText(this@CameraSettingsActivity, tr("Save failed", "保存失败"), Toast.LENGTH_SHORT).show()
                }
            } finally {
                saving = false
            }
        }
    }

    private fun saveCamera(
        name: String? = null,
        resolution: String? = null,
        location: String? = null,
    ) {
        if (saving) return
        model ?: return
        saving = true
        lifecycleScope.launch {
            try {
                val updated = withContext(Dispatchers.IO) {
                    repository.updateCameraSettings(
                        cameraId = cameraId,
                        name = name,
                        resolution = resolution,
                        location = location,
                        settings = null,
                    )
                }
                model = updated
                render(updated)
                Toast.makeText(this@CameraSettingsActivity, tr("Saved", "保存成功"), Toast.LENGTH_SHORT).show()
            } catch (ex: Exception) {
                if (!handleAuth(ex)) {
                    Toast.makeText(this@CameraSettingsActivity, tr("Save failed", "保存失败"), Toast.LENGTH_SHORT).show()
                }
            } finally {
                saving = false
            }
        }
    }

    private fun confirmRemove() {
        val currentName = model?.name ?: cameraName
        MaterialAlertDialogBuilder(this)
            .setTitle(tr("Remove Camera?", "移除摄像头？"))
            .setMessage(tr("This will remove $currentName from your account.", "这会将 $currentName 从你的账号中移除。"))
            .setNegativeButton(tr("Cancel", "取消"), null)
            .setPositiveButton(tr("Remove", "移除")) { _, _ ->
                if (cameraId <= 0L) return@setPositiveButton
                lifecycleScope.launch {
                    try {
                        withContext(Dispatchers.IO) { repository.deleteCamera(cameraId) }
                        finish()
                    } catch (ex: Exception) {
                        if (ex is HttpException && ex.code() == 401) {
                            appConfig.clearAuth()
                            startActivity(Intent(this@CameraSettingsActivity, LoginActivity::class.java))
                            finish()
                        } else {
                            Toast.makeText(this@CameraSettingsActivity, tr("Remove failed", "删除失败"), Toast.LENGTH_SHORT).show()
                        }
                    }
                }
            }
            .show()
    }

    private fun bindSwitch(containerId: Int, knobId: Int, enabled: Boolean) {
        val container = findViewById<FrameLayout>(containerId)
        val knob = findViewById<View>(knobId)
        container.setBackgroundResource(if (enabled) R.drawable.bg_switch_on else R.drawable.bg_switch_off)
        knob.setBackgroundResource(if (enabled) R.drawable.bg_switch_knob_on else R.drawable.bg_switch_knob_off)
        val params = knob.layoutParams as FrameLayout.LayoutParams
        params.gravity = if (enabled) Gravity.END or Gravity.CENTER_VERTICAL else Gravity.START or Gravity.CENTER_VERTICAL
        params.marginStart = if (enabled) 0 else 5.dp()
        params.marginEnd = if (enabled) 5.dp() else 0
        knob.layoutParams = params
    }

    private fun resolutionLabel(raw: String): String {
        return when (raw.lowercase()) {
            "720p" -> "720p (1280x720)"
            "1080p" -> "1080p (1920x1080)"
            "2k" -> "2K (2560x1440)"
            "4k" -> "4K (3840x2160)"
            else -> raw
        }
    }

    private fun motionSensitivityLabel(raw: String): String {
        return when (raw.lowercase()) {
            "low" -> tr("Low", "低")
            "medium" -> tr("Medium", "中")
            "high" -> tr("High", "高")
            else -> raw
        }
    }

    private fun soundSensitivityLabel(raw: String): String {
        return when (raw.lowercase()) {
            "quiet" -> tr("Quiet", "安静")
            "normal" -> tr("Normal", "普通")
            "loud" -> tr("Loud", "响亮")
            else -> raw
        }
    }

    private fun detectionZoneLabel(raw: String): String {
        return when (raw.lowercase()) {
            "1 zone configured" -> tr("1 zone configured", "已配置 1 个区域")
            "2 zones configured" -> tr("2 zones configured", "已配置 2 个区域")
            "3 zones configured" -> tr("3 zones configured", "已配置 3 个区域")
            "custom zones" -> tr("Custom zones", "自定义区域")
            else -> raw
        }
    }

    private fun nightModeLabel(raw: String): String {
        return when (raw.lowercase()) {
            "auto" -> tr("Auto", "自动")
            "on" -> tr("On", "开启")
            "off" -> tr("Off", "关闭")
            else -> raw
        }
    }

    private fun imageFlipLabel(raw: String): String {
        return when (raw.lowercase()) {
            "normal" -> tr("Normal", "正常")
            "flip horizontal" -> tr("Flip Horizontal", "水平翻转")
            "flip vertical" -> tr("Flip Vertical", "垂直翻转")
            "rotate 180" -> tr("Rotate 180", "旋转 180°")
            else -> raw
        }
    }

    private fun handleAuth(ex: Exception): Boolean {
        if (ex is HttpException && ex.code() == 401) {
            appConfig.clearAuth()
            startActivity(Intent(this, LoginActivity::class.java))
            finish()
            return true
        }
        return false
    }

    private fun Int.dp(): Int = (this * resources.displayMetrics.density).toInt()

    private fun applyLocalizedTexts() {
        findViewById<TextView>(R.id.camera_settings_page_title).text = tr("Camera Settings", "摄像头设置")
        findViewById<TextView>(R.id.camera_settings_section_general).text = tr("General", "通用")
        findViewById<TextView>(R.id.camera_settings_section_detection).text = tr("Detection", "检测")
        findViewById<TextView>(R.id.camera_settings_section_video).text = tr("Video", "视频")
        findViewById<TextView>(R.id.camera_settings_section_network).text = tr("Network", "网络")
        findViewById<TextView>(R.id.camera_settings_remove).text = tr("Remove Camera", "移除摄像头")
        findViewById<TextView>(R.id.camera_settings_name_value).text = tr("Camera Name", "摄像头名称")
        findViewById<TextView>(R.id.camera_settings_name_subtitle).text = cameraName
        findViewById<TextView>(R.id.camera_settings_location_value).text = tr("Location", "位置")
        findViewById<TextView>(R.id.camera_settings_location_subtitle).text = tr("Not set", "未设置")
        findViewById<TextView>(R.id.camera_settings_resolution_value).text = tr("Resolution", "分辨率")
        findViewById<TextView>(R.id.camera_settings_resolution_subtitle).text = resolutionLabel("1080p")
        findViewById<TextView>(R.id.camera_settings_motion_title).text = tr("Motion Detection", "移动检测")
        findViewById<TextView>(R.id.camera_settings_motion_value).text = "${tr("Sensitivity", "灵敏度")}: ${motionSensitivityLabel("High")}"
        findViewById<TextView>(R.id.camera_settings_person_title).text = tr("Person Detection", "人物检测")
        findViewById<TextView>(R.id.camera_settings_person_value).text = tr("AI-powered alerts", "AI 告警")
        findViewById<TextView>(R.id.camera_settings_sound_title).text = tr("Sound Detection", "声音检测")
        findViewById<TextView>(R.id.camera_settings_sound_value).text = tr("Disabled", "已关闭")
        findViewById<TextView>(R.id.camera_settings_zone_value).text = tr("Detection Zones", "检测区域")
        findViewById<TextView>(R.id.camera_settings_zone_subtitle).text = detectionZoneLabel("2 zones configured")
        findViewById<TextView>(R.id.camera_settings_night_title).text = tr("Night Vision", "夜视")
        findViewById<TextView>(R.id.camera_settings_night_value).text = nightModeLabel("Auto")
        findViewById<TextView>(R.id.camera_settings_flip_value).text = tr("Image Flip", "图像翻转")
        findViewById<TextView>(R.id.camera_settings_flip_subtitle).text = imageFlipLabel("Normal")
        findViewById<TextView>(R.id.camera_settings_watermark_title).text = tr("Watermark", "水印")
        findViewById<TextView>(R.id.camera_settings_watermark_value).text = tr("Timestamp enabled", "时间戳已启用")
        findViewById<TextView>(R.id.camera_settings_firmware_title).text = tr("Firmware", "固件")
        findViewById<TextView>(R.id.camera_settings_firmware_value).text = "v2.3.8 · ${tr("Update available", "可更新")}"
        findViewById<TextView>(R.id.camera_settings_firmware_action).text = tr("Update", "更新")
        findViewById<TextView>(R.id.camera_settings_wifi_title).text = "Wi-Fi"
        findViewById<TextView>(R.id.camera_settings_wifi_value).text = "${tr("Wi-Fi", "Wi-Fi")} · ${tr("Good", "良好")}"
        findViewById<TextView>(R.id.camera_settings_status_text).text = tr("Online", "在线")
    }

    private fun zh(): Boolean = isChineseLanguage(appConfig.getAppLanguage())

    private fun tr(en: String, zh: String): String = if (this.zh()) zh else en

    private fun isChineseLanguage(languageCode: String?): Boolean {
        if (languageCode.isNullOrBlank()) return false
        return languageCode.lowercase(Locale.US).startsWith("zh")
    }

    companion object {
        const val EXTRA_CAMERA_ID = "extra_camera_id"
        const val EXTRA_CAMERA_NAME = "extra_camera_name"
    }
}
