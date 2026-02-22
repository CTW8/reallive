package com.reallive.android.ui.settings

import android.content.Intent
import android.os.Bundle
import android.view.View
import android.widget.LinearLayout
import android.widget.TextView
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import androidx.lifecycle.lifecycleScope
import com.reallive.android.R
import com.reallive.android.config.AppConfig
import com.reallive.android.data.CameraRepository
import com.reallive.android.network.ApiClient
import com.reallive.android.network.StorageDeviceDto
import com.reallive.android.ui.auth.LoginActivity
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.async
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import retrofit2.HttpException

class StorageActivity : AppCompatActivity() {
    private lateinit var appConfig: AppConfig
    private lateinit var repository: CameraRepository

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        appConfig = AppConfig(this)
        if (appConfig.getToken().isNullOrBlank()) {
            finish()
            return
        }
        repository = CameraRepository(ApiClient.create(appConfig.getBaseUrl(), appConfig::getToken))

        setContentView(R.layout.activity_storage)
        applyLocalizedTexts()
        findViewById<android.view.View>(R.id.storage_back).setOnClickListener { finish() }
        findViewById<android.view.View>(R.id.storage_upgrade_btn).setOnClickListener {
            startActivity(Intent(this, UpgradePlanActivity::class.java))
        }
    }

    override fun onStart() {
        super.onStart()
        loadStorage()
    }

    private fun loadStorage() {
        lifecycleScope.launch {
            try {
                val pair = withContext(Dispatchers.IO) {
                    val overviewDeferred = async { repository.getStorageOverview() }
                    val byDeviceDeferred = async { repository.getStorageByDevice() }
                    overviewDeferred.await() to byDeviceDeferred.await()
                }
                val overview = pair.first
                val devices = pair.second.sortedByDescending { it.usedGb }
                bindOverview(overview.usedPercent, overview.used, overview.total)
                bindDevices(devices)
            } catch (ex: Exception) {
                if (ex is HttpException && ex.code() == 401) {
                    appConfig.clearAuth()
                    startActivity(Intent(this@StorageActivity, LoginActivity::class.java))
                    finish()
                } else {
                    Toast.makeText(this@StorageActivity, "加载存储信息失败", Toast.LENGTH_SHORT).show()
                }
            }
        }
    }

    private fun bindOverview(usedPercent: Double, usedGb: Double, totalGb: Double) {
        findViewById<TextView>(R.id.storage_total_percent).text = "${usedPercent.toInt()}%"
        findViewById<TextView>(R.id.storage_total_detail).text =
            "${formatGb(usedGb)} / ${formatGb(totalGb)} GB"
    }

    private fun bindDevices(devices: List<StorageDeviceDto>) {
        val rows = listOf(
            CameraRowBinding(
                nameId = R.id.storage_cam1_name,
                usedId = R.id.storage_cam1_used,
                fillId = R.id.storage_cam1_bar_fill,
            ),
            CameraRowBinding(
                nameId = R.id.storage_cam2_name,
                usedId = R.id.storage_cam2_used,
                fillId = R.id.storage_cam2_bar_fill,
            ),
            CameraRowBinding(
                nameId = R.id.storage_cam3_name,
                usedId = R.id.storage_cam3_used,
                fillId = R.id.storage_cam3_bar_fill,
            ),
            CameraRowBinding(
                nameId = R.id.storage_cam4_name,
                usedId = R.id.storage_cam4_used,
                fillId = R.id.storage_cam4_bar_fill,
            ),
            CameraRowBinding(
                nameId = R.id.storage_cam5_name,
                usedId = R.id.storage_cam5_used,
                fillId = R.id.storage_cam5_bar_fill,
            ),
        )

        rows.forEachIndexed { idx, row ->
            val d = devices.getOrNull(idx)
            val nameView = findViewById<TextView>(row.nameId)
            val usedView = findViewById<TextView>(row.usedId)
            val fillView = findViewById<View>(row.fillId)
            val params = fillView.layoutParams as LinearLayout.LayoutParams
            if (d == null) {
                nameView.text = "--"
                usedView.text = "0 GB"
                params.weight = 0.1f
            } else {
                nameView.text = d.name
                usedView.text = "${formatGb(d.usedGb)} GB"
                val usedRatio = ((d.usedPercent / 100.0).coerceIn(0.02, 1.0)).toFloat()
                params.weight = usedRatio * 100f
            }
            fillView.layoutParams = params
        }
    }

    private fun formatGb(v: Double): String {
        val rounded = kotlin.math.round(v * 10.0) / 10.0
        val asInt = rounded.toInt().toDouble()
        return if (rounded == asInt) rounded.toInt().toString() else rounded.toString()
    }

    private data class CameraRowBinding(
        val nameId: Int,
        val usedId: Int,
        val fillId: Int,
    )

    private fun applyLocalizedTexts() {
        val zh = appConfig.getAppLanguage().startsWith("zh", true)
        if (!zh) return
        findViewById<TextView>(R.id.storage_page_title).text = "云存储"
        findViewById<TextView>(R.id.storage_used_label).text = "已用"
        findViewById<TextView>(R.id.storage_free_label).text = "剩余"
        findViewById<TextView>(R.id.storage_by_camera_title).text = "按摄像头统计"
        findViewById<TextView>(R.id.storage_auto_delete_text).text = "自动删除 30 天前历史录像"
        findViewById<TextView>(R.id.storage_upgrade_btn).text = "升级到 500 GB 套餐"
    }
}
