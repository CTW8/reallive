package com.reallive.android.ui.camera

import android.Manifest
import android.animation.ArgbEvaluator
import android.animation.ObjectAnimator
import android.animation.ValueAnimator
import android.content.pm.PackageManager
import android.content.Intent
import android.net.Uri
import android.os.Bundle
import android.text.InputType
import android.view.LayoutInflater
import android.view.View
import android.widget.EditText
import android.widget.ImageButton
import android.widget.RadioGroup
import android.widget.TextView
import android.widget.Toast
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AlertDialog
import androidx.appcompat.app.AppCompatActivity
import androidx.camera.core.Camera
import androidx.camera.core.CameraSelector
import androidx.camera.core.ImageAnalysis
import androidx.camera.core.Preview
import androidx.camera.lifecycle.ProcessCameraProvider
import androidx.camera.view.PreviewView
import androidx.core.content.ContextCompat
import androidx.core.widget.doAfterTextChanged
import com.google.mlkit.vision.barcode.BarcodeScannerOptions
import com.google.mlkit.vision.barcode.BarcodeScanning
import com.google.mlkit.vision.barcode.common.Barcode
import com.google.mlkit.vision.common.InputImage
import com.google.android.material.bottomsheet.BottomSheetDialog
import com.reallive.android.R
import com.reallive.android.config.AppConfig
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import java.util.concurrent.Executors
import android.view.animation.LinearInterpolator

class AddCameraActivity : AppCompatActivity() {
    private lateinit var appConfig: AppConfig
    private var isZh: Boolean = false
    private lateinit var previewView: PreviewView
    private lateinit var flashButton: ImageButton
    private lateinit var scanBox: View
    private lateinit var scanLine: View
    private var cameraProvider: ProcessCameraProvider? = null
    private var camera: Camera? = null
    private var torchEnabled: Boolean = false
    private val cameraExecutor = Executors.newSingleThreadExecutor()
    private val barcodeScanner by lazy {
        BarcodeScanning.getClient(
            BarcodeScannerOptions.Builder()
                .setBarcodeFormats(
                    Barcode.FORMAT_QR_CODE,
                    Barcode.FORMAT_AZTEC,
                    Barcode.FORMAT_DATA_MATRIX,
                )
                .build(),
        )
    }
    private var handlingScan: Boolean = false
    private var lastScanValue: String = ""
    private var lastScanAtMs: Long = 0L
    private var scanPaused: Boolean = false
    private var scanCooldownUntilMs: Long = 0L
    private var scanLineAnimator: ValueAnimator? = null

    private val cameraPermissionLauncher = registerForActivityResult(
        ActivityResultContracts.RequestPermission(),
    ) { granted ->
        if (granted) {
            startCameraPreview()
        } else {
            Toast.makeText(
                this,
                if (isZh) "未授予相机权限，无法显示取景画面" else "Camera permission denied, preview unavailable",
                Toast.LENGTH_SHORT,
            ).show()
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        appConfig = AppConfig(this)
        if (appConfig.getToken().isNullOrBlank()) {
            finish()
            return
        }
        isZh = isChineseLanguage(appConfig.getAppLanguage())

        setContentView(R.layout.activity_add_camera)
        previewView = findViewById(R.id.add_camera_preview)
        flashButton = findViewById(R.id.add_camera_flash)
        scanBox = findViewById(R.id.add_camera_scan_box)
        scanLine = findViewById(R.id.add_camera_scan_line)
        applyLocalizedTexts(isZh)
        findViewById<android.view.View>(R.id.add_camera_close).setOnClickListener { finish() }
        flashButton.setOnClickListener { toggleFlash() }
        updateFlashUi()

        findViewById<View>(R.id.add_camera_scan_btn).setOnClickListener {
            showCameraCreateDialog(source = "manual", prefilledStreamKey = null)
        }
        findViewById<View>(R.id.add_camera_nearby_btn).setOnClickListener {
            showNearbyPickerDialog()
        }
    }

    override fun onStart() {
        super.onStart()
        ensureCameraPermissionAndStart()
        startScanLineAnimation()
    }

    override fun onStop() {
        super.onStop()
        cameraProvider?.unbindAll()
        camera = null
        torchEnabled = false
        handlingScan = false
        scanPaused = true
        stopScanLineAnimation()
        updateFlashUi()
    }

    override fun onDestroy() {
        super.onDestroy()
        cameraExecutor.shutdown()
        barcodeScanner.close()
    }

    private fun showCameraCreateDialog(source: String, prefilledStreamKey: String?) {
        scanPaused = true
        val sheet = BottomSheetDialog(this)
        val content = LayoutInflater.from(this).inflate(R.layout.dialog_camera_create_sheet, null, false)
        sheet.setContentView(content)

        val title = content.findViewById<TextView>(R.id.camera_create_title)
        val subtitle = content.findViewById<TextView>(R.id.camera_create_subtitle)
        val sourceText = content.findViewById<TextView>(R.id.camera_create_source)
        val nameInput = content.findViewById<EditText>(R.id.camera_create_name)
        val keyInput = content.findViewById<EditText>(R.id.camera_create_stream_key)
        val keyHint = content.findViewById<TextView>(R.id.camera_create_stream_key_hint)
        val resolutionLabel = content.findViewById<TextView>(R.id.camera_create_resolution_label)
        val group = content.findViewById<RadioGroup>(R.id.camera_create_resolution_group)
        val cancel = content.findViewById<TextView>(R.id.camera_create_cancel)
        val start = content.findViewById<TextView>(R.id.camera_create_start)

        title.text = if (source == "scan") {
            if (isZh) "确认摄像头信息" else "Confirm Camera Setup"
        } else {
            if (isZh) "手动添加摄像头" else "Add Camera Manually"
        }
        subtitle.text = if (isZh) "连接前请确认设备信息" else "Review camera info before connecting"
        sourceText.text = when (source.lowercase(Locale.US)) {
            "scan" -> if (isZh) "来源：二维码识别" else "Source: QR Scan"
            "nearby" -> if (isZh) "来源：附近设备" else "Source: Nearby Device"
            else -> if (isZh) "来源：手动输入" else "Source: Manual"
        }
        nameInput.hint = if (isZh) "例如：客厅摄像头" else "e.g. Living Room Camera"
        nameInput.inputType = InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_FLAG_CAP_WORDS
        nameInput.setText(defaultCameraName())
        keyInput.hint = if (isZh) "设备码（可选）" else "Device code / Stream key (optional)"
        keyInput.inputType = InputType.TYPE_CLASS_TEXT
        if (!prefilledStreamKey.isNullOrBlank()) keyInput.setText(prefilledStreamKey)
        keyHint.text = if (isZh) "留空将自动创建新的推流码" else "Leave empty to create a new stream key"

        resolutionLabel.text = if (isZh) "分辨率" else "Resolution"
        if (prefilledStreamKey.isNullOrBlank()) {
            content.findViewById<View>(R.id.camera_create_res_720p).isSelected = true
            group.check(R.id.camera_create_res_720p)
        } else {
            group.check(R.id.camera_create_res_1080p)
        }
        cancel.text = if (isZh) "取消" else "Cancel"
        start.text = if (isZh) "开始连接" else "Start Setup"
        updateStartButtonEnabled(start, false)

        fun isInputValid(): Boolean {
            val name = nameInput.text?.toString()?.trim().orEmpty()
            if (name.isBlank()) return false
            val streamKey = keyInput.text?.toString()?.trim().orEmpty()
            if (source == "scan" && streamKey.isBlank()) return false
            if (streamKey.isNotBlank() && !Regex("^[a-zA-Z0-9._-]{8,128}$").matches(streamKey)) return false
            return true
        }

        val refreshValidation = { updateStartButtonEnabled(start, isInputValid()) }
        nameInput.doAfterTextChanged { refreshValidation() }
        keyInput.doAfterTextChanged { refreshValidation() }
        refreshValidation()

        cancel.setOnClickListener { sheet.dismiss() }
        start.setOnClickListener {
            if (!isInputValid()) return@setOnClickListener
            val name = nameInput.text?.toString()?.trim().orEmpty().ifBlank { defaultCameraName() }
            val streamKey = keyInput.text?.toString()?.trim().orEmpty()
            if (streamKey.isNotBlank() && !Regex("^[a-zA-Z0-9._-]{8,128}$").matches(streamKey)) {
                Toast.makeText(
                    this,
                    if (isZh) "设备码格式错误" else "Invalid device code format",
                    Toast.LENGTH_SHORT,
                ).show()
                return@setOnClickListener
            }
            val resolution = resolutionFromChecked(group.checkedRadioButtonId)
            sheet.dismiss()
            startConnectFlow(name, resolution, source, streamKey)
        }
        sheet.setOnDismissListener {
            scanPaused = false
            scanCooldownUntilMs = System.currentTimeMillis() + 1200L
        }
        sheet.show()
    }

    private fun updateStartButtonEnabled(button: TextView, enabled: Boolean) {
        button.isEnabled = enabled
        button.alpha = if (enabled) 1f else 0.65f
        button.setBackgroundResource(if (enabled) R.drawable.bg_btn_primary else R.drawable.bg_btn_primary_disabled)
    }

    private fun resolutionFromChecked(id: Int): String {
        return when (id) {
            R.id.camera_create_res_360p -> "360p"
            R.id.camera_create_res_540p -> "540p"
            R.id.camera_create_res_1080p -> "1080p"
            else -> "720p"
        }
    }

    private fun showNearbyPickerDialog() {
        val fmt = if (isZh) "附近设备 %s" else "Nearby Device %s"
        val suffix = SimpleDateFormat("mmss", Locale.US).format(Date())
        val devices = arrayOf(
            String.format(fmt, "A-$suffix"),
            String.format(fmt, "B-$suffix"),
            String.format(fmt, "C-$suffix"),
        )
        AlertDialog.Builder(this)
            .setTitle(if (isZh) "选择附近设备" else "Select Nearby Device")
            .setItems(devices) { _, which ->
                val name = if (isZh) "摄像头 ${which + 1}" else "Camera ${which + 1}"
                Toast.makeText(
                    this,
                    if (isZh) "正在连接 ${devices[which]}" else "Connecting ${devices[which]}",
                    Toast.LENGTH_SHORT,
                ).show()
                startConnectFlow(name, "1080p", "nearby", null)
            }
            .setNegativeButton(if (isZh) "取消" else "Cancel", null)
            .show()
    }

    private fun startConnectFlow(name: String, resolution: String, source: String, streamKey: String?) {
        startActivity(
            Intent(this, CameraConnectProgressActivity::class.java).apply {
                putExtra(CameraConnectProgressActivity.EXTRA_CAMERA_NAME, name)
                putExtra(CameraConnectProgressActivity.EXTRA_CAMERA_RESOLUTION, resolution)
                putExtra(CameraConnectProgressActivity.EXTRA_SETUP_SOURCE, source)
                if (!streamKey.isNullOrBlank()) {
                    putExtra(CameraConnectProgressActivity.EXTRA_STREAM_KEY, streamKey)
                }
            },
        )
    }

    private fun ensureCameraPermissionAndStart() {
        val granted = ContextCompat.checkSelfPermission(this, Manifest.permission.CAMERA) == PackageManager.PERMISSION_GRANTED
        if (granted) {
            startCameraPreview()
        } else {
            cameraPermissionLauncher.launch(Manifest.permission.CAMERA)
        }
    }

    private fun startCameraPreview() {
        val providerFuture = ProcessCameraProvider.getInstance(this)
        providerFuture.addListener(
            {
                val provider = runCatching { providerFuture.get() }.getOrNull() ?: return@addListener
                cameraProvider = provider
                val preview = Preview.Builder().build().also {
                    it.setSurfaceProvider(previewView.surfaceProvider)
                }
                val analysis = ImageAnalysis.Builder()
                    .setBackpressureStrategy(ImageAnalysis.STRATEGY_KEEP_ONLY_LATEST)
                    .build()
                    .also { analyzer ->
                        analyzer.setAnalyzer(cameraExecutor) { imageProxy ->
                            processScanFrame(imageProxy)
                        }
                    }
                val selector = CameraSelector.DEFAULT_BACK_CAMERA
                runCatching {
                    provider.unbindAll()
                    camera = provider.bindToLifecycle(this, selector, preview, analysis)
                    if (torchEnabled && camera?.cameraInfo?.hasFlashUnit() == true) {
                        camera?.cameraControl?.enableTorch(true)
                    }
                }.onFailure {
                    Toast.makeText(
                        this,
                        if (isZh) "无法启动相机预览" else "Failed to start camera preview",
                        Toast.LENGTH_SHORT,
                    ).show()
                }
                updateFlashUi()
            },
            ContextCompat.getMainExecutor(this),
        )
    }

    private fun toggleFlash() {
        val current = camera ?: return
        if (current.cameraInfo.hasFlashUnit() != true) {
            Toast.makeText(
                this,
                if (isZh) "当前设备不支持闪光灯" else "Flash is not available on this device",
                Toast.LENGTH_SHORT,
            ).show()
            return
        }
        torchEnabled = !torchEnabled
        current.cameraControl.enableTorch(torchEnabled)
        updateFlashUi()
    }

    private fun updateFlashUi() {
        val enabled = torchEnabled && (camera?.cameraInfo?.hasFlashUnit() == true)
        flashButton.alpha = if (enabled) 1f else 0.65f
        flashButton.contentDescription = if (enabled) {
            if (isZh) "关闭闪光灯" else "Turn flash off"
        } else {
            if (isZh) "开启闪光灯" else "Turn flash on"
        }
    }

    private fun processScanFrame(imageProxy: androidx.camera.core.ImageProxy) {
        if (scanPaused || System.currentTimeMillis() < scanCooldownUntilMs) {
            imageProxy.close()
            return
        }
        val mediaImage = imageProxy.image
        if (mediaImage == null) {
            imageProxy.close()
            return
        }
        val image = InputImage.fromMediaImage(mediaImage, imageProxy.imageInfo.rotationDegrees)
        barcodeScanner.process(image)
            .addOnSuccessListener { codes ->
                val raw = selectBestBarcode(codes, image.width, image.height)
                    ?.rawValue
                    ?.trim()
                    ?.takeIf { s -> s.isNotEmpty() }
                if (!raw.isNullOrBlank()) {
                    onQrDetected(raw)
                }
            }
            .addOnCompleteListener { imageProxy.close() }
    }

    private fun selectBestBarcode(codes: List<Barcode>, imageW: Int, imageH: Int): Barcode? {
        if (codes.isEmpty()) return null
        val cx = imageW / 2f
        val cy = imageH / 2f
        val centralHalfW = imageW * 0.20f
        val centralHalfH = imageH * 0.20f

        var bestInCenter: Barcode? = null
        var bestInCenterDist = Float.MAX_VALUE
        var bestAny: Barcode? = null
        var bestAnyDist = Float.MAX_VALUE

        codes.forEach { code ->
            val box = code.boundingBox ?: return@forEach
            val bx = (box.left + box.right) / 2f
            val by = (box.top + box.bottom) / 2f
            val dx = bx - cx
            val dy = by - cy
            val dist = dx * dx + dy * dy

            if (dist < bestAnyDist) {
                bestAnyDist = dist
                bestAny = code
            }

            val inCenter = kotlin.math.abs(dx) <= centralHalfW && kotlin.math.abs(dy) <= centralHalfH
            if (inCenter && dist < bestInCenterDist) {
                bestInCenterDist = dist
                bestInCenter = code
            }
        }
        return bestInCenter ?: bestAny
    }

    private fun onQrDetected(raw: String) {
        val now = System.currentTimeMillis()
        if (scanPaused || now < scanCooldownUntilMs) return
        if (handlingScan) return
        if (raw == lastScanValue && now - lastScanAtMs < 3000L) return
        val streamKey = extractStreamKey(raw) ?: return
        if (!Regex("^[a-zA-Z0-9._-]{8,128}$").matches(streamKey)) return
        lastScanValue = raw
        lastScanAtMs = now
        handlingScan = true
        runOnUiThread {
            playScanHitFeedback()
            showCameraCreateDialog(source = "scan", prefilledStreamKey = streamKey)
            handlingScan = false
        }
    }

    private fun playScanHitFeedback() {
        val startColor = 0x66C8BFFF.toInt()
        val hitColor = 0xFFF1E3FF.toInt()

        ObjectAnimator.ofObject(
            scanLine,
            "backgroundColor",
            ArgbEvaluator(),
            startColor,
            hitColor,
            startColor,
        ).apply {
            duration = 520L
            start()
        }

        ObjectAnimator.ofFloat(scanBox, "alpha", 1f, 0.82f, 1f).apply {
            duration = 520L
            start()
        }
    }

    private fun startScanLineAnimation() {
        scanBox.post {
            val boxHeight = scanBox.height.toFloat()
            val lineHeight = scanLine.height.toFloat().coerceAtLeast(2f)
            if (boxHeight <= lineHeight) return@post
            val range = (boxHeight - lineHeight) / 2f - 4f
            if (range <= 0f) return@post

            scanLineAnimator?.cancel()
            scanLine.translationY = -range
            scanLineAnimator = ValueAnimator.ofFloat(-range, range).apply {
                duration = 1800L
                repeatCount = ValueAnimator.INFINITE
                repeatMode = ValueAnimator.RESTART
                interpolator = LinearInterpolator()
                addUpdateListener { anim ->
                    scanLine.translationY = (anim.animatedValue as Float)
                }
                start()
            }
        }
    }

    private fun stopScanLineAnimation() {
        scanLineAnimator?.cancel()
        scanLineAnimator = null
    }

    private fun extractStreamKey(raw: String): String? {
        val text = raw.trim()
        if (text.isEmpty()) return null
        if (!text.contains("://")) return text
        val uri = runCatching { Uri.parse(text) }.getOrNull() ?: return null
        val candidates = listOf("streamKey", "stream_key", "key", "sk")
        candidates.forEach { key ->
            val value = uri.getQueryParameter(key)?.trim()
            if (!value.isNullOrBlank()) return value
        }
        val last = uri.lastPathSegment?.trim().orEmpty()
        if (last.endsWith(".flv", true)) return last.removeSuffix(".flv")
        return last.ifBlank { null }
    }

    private fun defaultCameraName(): String {
        val suffix = SimpleDateFormat("HH:mm", Locale.US).format(Date())
        return if (isZh) "新摄像头 $suffix" else "New Camera $suffix"
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
