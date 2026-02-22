package com.reallive.android.ui.settings

import android.content.Intent
import android.os.Bundle
import android.view.Gravity
import android.view.View
import android.view.ViewGroup
import android.widget.LinearLayout
import android.widget.TextView
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import androidx.lifecycle.lifecycleScope
import com.reallive.android.R
import com.reallive.android.config.AppConfig
import com.reallive.android.data.CameraRepository
import com.reallive.android.network.ApiClient
import com.reallive.android.network.StorageCloudDto
import com.reallive.android.network.StoragePlanDto
import com.reallive.android.network.StoragePlansResponse
import com.reallive.android.ui.auth.LoginActivity
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.async
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import retrofit2.HttpException

class UpgradePlanActivity : AppCompatActivity() {
    private lateinit var appConfig: AppConfig
    private lateinit var repository: CameraRepository
    private var cloudConfig: StorageCloudDto? = null
    private var plans: List<StoragePlanDto> = emptyList()
    private var currentPlanId: String? = null
    private var selectedPlanId: String? = null
    private var selectedPlanGb: Double = 500.0
    private var saving = false
    private val dynamicPlanCards = linkedMapOf<String, View>()

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        appConfig = AppConfig(this)
        if (appConfig.getToken().isNullOrBlank()) {
            finish()
            return
        }
        repository = CameraRepository(ApiClient.create(appConfig.getBaseUrl(), appConfig::getToken))
        setContentView(R.layout.activity_upgrade_plan)
        findViewById<android.view.View>(R.id.upgrade_back).setOnClickListener { finish() }
        findViewById<View>(R.id.upgrade_plan_plus_card).visibility = View.GONE
        findViewById<View>(R.id.upgrade_plan_business_card).visibility = View.GONE
        findViewById<View>(R.id.upgrade_continue).setOnClickListener { applySelectedPlan() }
    }

    override fun onStart() {
        super.onStart()
        loadCloudConfig()
    }

    private fun loadCloudConfig() {
        lifecycleScope.launch {
            try {
                val (cloud, plansResp) = withContext(Dispatchers.IO) {
                    val cloudDeferred = async { repository.getStorageCloudConfig() }
                    val plansDeferred = async { repository.getStoragePlans() }
                    cloudDeferred.await() to plansDeferred.await()
                }
                cloudConfig = cloud
                currentPlanId = plansResp.currentPlanId
                plans = plansResp.plans
                selectedPlanGb = initialSelectedPlanGb(cloud, plansResp)
                selectedPlanId = plans.firstOrNull { it.totalGb == selectedPlanGb }?.id
                bindCloud(cloud)
                bindPlans()
                refreshPlanSelection()
            } catch (ex: Exception) {
                if (ex is HttpException && ex.code() == 401) {
                    forceRelogin()
                } else {
                    Toast.makeText(this@UpgradePlanActivity, "加载套餐信息失败", Toast.LENGTH_SHORT).show()
                }
            }
        }
    }

    private fun bindCloud(cloud: StorageCloudDto) {
        val planName = plans.firstOrNull { it.id == currentPlanId }?.let { "${it.name} Cloud" } ?: when {
            cloud.totalGb >= 1000.0 -> "Business · 1 TB Cloud"
            cloud.totalGb >= 500.0 -> "Plus · 500 GB Cloud"
            else -> "Pro · ${formatGb(cloud.totalGb)} GB Cloud"
        }
        findViewById<TextView>(R.id.upgrade_current_plan_value).text = planName
        findViewById<TextView>(R.id.upgrade_current_usage).text = "${cloud.usedPercent.toInt()}% used"
    }

    private fun bindPlans() {
        if (plans.isNotEmpty()) {
            bindDynamicPlans(plans)
            return
        }
        val plus = plans.firstOrNull { it.totalGb == 500.0 } ?: StoragePlanDto(
            id = "plus-500",
            name = "Plus 500 GB",
            totalGb = 500.0,
            priceMonthlyUsd = 9.99,
            description = "Up to 32 cameras · 180-day retention · AI event filters",
        )
        val biz = plans.firstOrNull { it.totalGb == 1000.0 } ?: StoragePlanDto(
            id = "business-1tb",
            name = "Business 1 TB",
            totalGb = 1000.0,
            priceMonthlyUsd = 19.99,
            description = "Unlimited users · Shared roles · Priority support",
        )
        findViewById<TextView>(R.id.upgrade_plus_name).text = plus.name
        findViewById<TextView>(R.id.upgrade_plus_price).text = "$${formatPrice(plus.priceMonthlyUsd)}/mo"
        findViewById<TextView>(R.id.upgrade_plus_desc).text = plus.description

        findViewById<TextView>(R.id.upgrade_business_name).text = biz.name
        findViewById<TextView>(R.id.upgrade_business_price).text = "$${formatPrice(biz.priceMonthlyUsd)}/mo"
        findViewById<TextView>(R.id.upgrade_business_desc).text = biz.description
    }

    private fun bindDynamicPlans(items: List<StoragePlanDto>) {
        val container = findViewById<LinearLayout>(R.id.upgrade_plan_list_container)
        container.removeAllViews()
        dynamicPlanCards.clear()
        items.forEachIndexed { index, plan ->
            val card = LinearLayout(this).apply {
                orientation = LinearLayout.VERTICAL
                background = getDrawable(R.drawable.bg_settings_card)
                setPadding(12.dp(), 12.dp(), 12.dp(), 12.dp())
                layoutParams = LinearLayout.LayoutParams(
                    ViewGroup.LayoutParams.MATCH_PARENT,
                    ViewGroup.LayoutParams.WRAP_CONTENT,
                ).apply {
                    if (index > 0) topMargin = 10.dp()
                }
            }
            val topRow = LinearLayout(this).apply {
                orientation = LinearLayout.HORIZONTAL
                gravity = Gravity.CENTER_VERTICAL
                layoutParams = LinearLayout.LayoutParams(
                    ViewGroup.LayoutParams.MATCH_PARENT,
                    ViewGroup.LayoutParams.WRAP_CONTENT,
                )
            }
            val name = TextView(this).apply {
                text = plan.name
                setTextColor(0xFFFFFFFF.toInt())
                textSize = 20f
                layoutParams = LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f)
            }
            val price = TextView(this).apply {
                text = "$${formatPrice(plan.priceMonthlyUsd)}/mo"
                setTextColor(getColor(R.color.auth_primary))
                textSize = 16f
            }
            topRow.addView(name)
            topRow.addView(price)
            val desc = TextView(this).apply {
                text = plan.description
                setTextColor(getColor(R.color.auth_on_surface_variant))
                textSize = 12f
                layoutParams = LinearLayout.LayoutParams(
                    ViewGroup.LayoutParams.MATCH_PARENT,
                    ViewGroup.LayoutParams.WRAP_CONTENT,
                ).apply {
                    topMargin = 6.dp()
                }
            }
            card.addView(topRow)
            card.addView(desc)
            card.setOnClickListener {
                selectedPlanId = plan.id
                selectedPlanGb = plan.totalGb
                refreshPlanSelection()
            }
            container.addView(card)
            dynamicPlanCards[plan.id] = card
        }
    }

    private fun refreshPlanSelection() {
        val continueBtn = findViewById<TextView>(R.id.upgrade_continue)

        if (dynamicPlanCards.isNotEmpty()) {
            dynamicPlanCards.forEach { (id, view) ->
                view.setBackgroundResource(
                    if (selectedPlanId == id) R.drawable.bg_btn_tonal else R.drawable.bg_settings_card,
                )
            }
            val selectedName = plans.firstOrNull { it.id == selectedPlanId }?.name
                ?: "${formatGb(selectedPlanGb)} GB"
            continueBtn.text = "Apply $selectedName"
            return
        }

        val plus = findViewById<View>(R.id.upgrade_plan_plus_card)
        val business = findViewById<View>(R.id.upgrade_plan_business_card)
        val plusSelected = selectedPlanGb == 500.0
        plus.setBackgroundResource(if (plusSelected) R.drawable.bg_btn_tonal else R.drawable.bg_settings_card)
        business.setBackgroundResource(if (!plusSelected) R.drawable.bg_btn_tonal else R.drawable.bg_settings_card)
        continueBtn.text = if (plusSelected) "Apply 500 GB Plan" else "Apply 1 TB Plan"
    }

    private fun applySelectedPlan() {
        if (saving) return
        val current = cloudConfig ?: return
        saving = true
        lifecycleScope.launch {
            try {
                val updated = withContext(Dispatchers.IO) {
                    repository.updateStorageCloudConfig(
                        enabled = true,
                        provider = current.provider.ifBlank { "s3" },
                        bucket = current.bucket,
                        region = current.region,
                        endpoint = current.endpoint,
                        totalGb = selectedPlanGb,
                        usedGb = current.usedGb.coerceAtMost(selectedPlanGb),
                    )
                }
                cloudConfig = updated
                currentPlanId = plans.minByOrNull { kotlin.math.abs(it.totalGb - selectedPlanGb) }?.id
                selectedPlanId = currentPlanId
                bindCloud(updated)
                Toast.makeText(this@UpgradePlanActivity, "套餐已更新", Toast.LENGTH_SHORT).show()
            } catch (ex: Exception) {
                if (ex is HttpException && ex.code() == 401) {
                    forceRelogin()
                    return@launch
                }
                Toast.makeText(this@UpgradePlanActivity, "套餐更新失败", Toast.LENGTH_SHORT).show()
            } finally {
                saving = false
            }
        }
    }

    private fun formatGb(v: Double): String {
        val rounded = kotlin.math.round(v * 10.0) / 10.0
        val asInt = rounded.toInt().toDouble()
        return if (rounded == asInt) rounded.toInt().toString() else rounded.toString()
    }

    private fun formatPrice(v: Double): String {
        return String.format(java.util.Locale.US, "%.2f", v)
    }

    private fun initialSelectedPlanGb(cloud: StorageCloudDto, plansResp: StoragePlansResponse): Double {
        val fromId = plansResp.currentPlanId?.let { id ->
            plansResp.plans.firstOrNull { it.id == id }?.totalGb
        }
        if (fromId != null && fromId > 0.0) return fromId
        return if (cloud.totalGb >= 1000.0) 1000.0 else 500.0
    }

    private fun forceRelogin() {
        appConfig.clearAuth()
        startActivity(Intent(this, LoginActivity::class.java))
        finish()
    }

    private fun Int.dp(): Int = (this * resources.displayMetrics.density).toInt()
}
