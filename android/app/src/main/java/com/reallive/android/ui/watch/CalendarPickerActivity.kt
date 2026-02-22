package com.reallive.android.ui.watch

import android.app.Activity
import android.content.Intent
import android.os.Bundle
import android.widget.GridLayout
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import androidx.core.content.ContextCompat
import com.reallive.android.R
import com.reallive.android.config.AppConfig
import java.text.SimpleDateFormat
import java.util.Calendar
import java.util.Date
import java.util.Locale

class CalendarPickerActivity : AppCompatActivity() {
    private var selectedDayStartMs: Long = 0L
    private val dayViews = mutableListOf<TextView>()
    private val displayMonth = Calendar.getInstance()
    private lateinit var appConfig: AppConfig

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_calendar_picker)
        appConfig = AppConfig(this)

        selectedDayStartMs = intent.getLongExtra(EXTRA_SELECTED_DAY_START_MS, System.currentTimeMillis())
        displayMonth.timeInMillis = selectedDayStartMs
        displayMonth.set(Calendar.DAY_OF_MONTH, 1)
        applyLocalizedTexts(isChineseLanguage(appConfig.getAppLanguage()))
        bindCalendarActions()
        renderCalendarGrid()
        updateHeader()

        findViewById<android.view.View>(R.id.calendar_close).setOnClickListener { finish() }
        findViewById<android.view.View>(R.id.calendar_edit).setOnClickListener { openYearMonthPicker() }
        findViewById<TextView>(R.id.calendar_month_title).setOnClickListener { openYearMonthPicker() }
        findViewById<android.view.View>(R.id.calendar_picker_confirm).setOnClickListener {
            setResult(
                Activity.RESULT_OK,
                Intent().putExtra(RESULT_SELECTED_DAY_START_MS, selectedDayStartMs),
            )
            finish()
        }
        findViewById<android.view.View>(R.id.calendar_picker_cancel).setOnClickListener { finish() }
    }

    private fun bindCalendarActions() {
        findViewById<android.view.View>(R.id.calendar_prev_month).setOnClickListener {
            displayMonth.add(Calendar.MONTH, -1)
            renderCalendarGrid()
            updateHeader()
        }
        findViewById<android.view.View>(R.id.calendar_next_month).setOnClickListener {
            displayMonth.add(Calendar.MONTH, 1)
            renderCalendarGrid()
            updateHeader()
        }
    }

    private fun openYearMonthPicker() {
        val zh = isChineseLanguage(appConfig.getAppLanguage())
        val currentYear = displayMonth.get(Calendar.YEAR)
        val years = ((currentYear - 15)..(currentYear + 15)).toList()
        val yearLabels = years.map { it.toString() }.toTypedArray()
        val selectedYearIndex = years.indexOf(currentYear).coerceAtLeast(0)
        com.google.android.material.dialog.MaterialAlertDialogBuilder(this)
            .setTitle(if (zh) "选择年份" else "Select Year")
            .setSingleChoiceItems(yearLabels, selectedYearIndex) { dialog, yearIndex ->
                dialog.dismiss()
                openMonthPicker(years[yearIndex])
            }
            .setNegativeButton(if (zh) "取消" else "Cancel", null)
            .show()
    }

    private fun openMonthPicker(year: Int) {
        val zh = isChineseLanguage(appConfig.getAppLanguage())
        val monthLabels = (1..12).map { m ->
            Calendar.getInstance().apply {
                set(Calendar.MONTH, m - 1)
            }.getDisplayName(Calendar.MONTH, Calendar.SHORT, Locale.getDefault()) ?: m.toString()
        }.toTypedArray()
        val selectedMonthIndex = displayMonth.get(Calendar.MONTH).coerceIn(0, 11)
        com.google.android.material.dialog.MaterialAlertDialogBuilder(this)
            .setTitle(if (zh) "选择月份" else "Select Month")
            .setSingleChoiceItems(monthLabels, selectedMonthIndex) { dialog, monthIndex ->
                dialog.dismiss()
                applyYearMonthSelection(year, monthIndex)
            }
            .setNegativeButton(if (zh) "取消" else "Cancel", null)
            .show()
    }

    private fun applyYearMonthSelection(year: Int, month: Int) {
        displayMonth.set(Calendar.YEAR, year)
        displayMonth.set(Calendar.MONTH, month)
        displayMonth.set(Calendar.DAY_OF_MONTH, 1)

        val selected = Calendar.getInstance().apply { timeInMillis = selectedDayStartMs }
        val targetMaxDay = displayMonth.getActualMaximum(Calendar.DAY_OF_MONTH)
        val clampedDay = selected.get(Calendar.DAY_OF_MONTH).coerceIn(1, targetMaxDay)
        selected.set(Calendar.YEAR, year)
        selected.set(Calendar.MONTH, month)
        selected.set(Calendar.DAY_OF_MONTH, clampedDay)
        selected.set(Calendar.HOUR_OF_DAY, 0)
        selected.set(Calendar.MINUTE, 0)
        selected.set(Calendar.SECOND, 0)
        selected.set(Calendar.MILLISECOND, 0)
        selectedDayStartMs = selected.timeInMillis

        renderCalendarGrid()
        updateHeader()
    }

    private fun renderCalendarGrid() {
        val grid = findViewById<GridLayout>(R.id.calendar_grid)
        grid.removeAllViews()
        dayViews.clear()
        val dayHeaders = arrayOf("S", "M", "T", "W", "T", "F", "S")
        dayHeaders.forEach { label ->
            grid.addView(
                TextView(this).apply {
                    layoutParams = GridLayout.LayoutParams(
                        GridLayout.spec(GridLayout.UNDEFINED, 1f),
                        GridLayout.spec(GridLayout.UNDEFINED, 1f),
                    ).apply {
                        width = 0
                        height = 32.dp()
                    }
                    gravity = android.view.Gravity.CENTER
                    text = label
                    setTextColor(ContextCompat.getColor(this@CalendarPickerActivity, R.color.rl_text_muted))
                },
            )
        }
        val firstWeekDay = displayMonth.get(Calendar.DAY_OF_WEEK) - Calendar.SUNDAY
        val maxDay = displayMonth.getActualMaximum(Calendar.DAY_OF_MONTH)
        repeat(42) { index ->
            val dayNum = index - firstWeekDay + 1
            val inCurrentMonth = dayNum in 1..maxDay
            val dayCell = TextView(this).apply {
                layoutParams = GridLayout.LayoutParams(
                    GridLayout.spec(GridLayout.UNDEFINED, 1f),
                    GridLayout.spec(GridLayout.UNDEFINED, 1f),
                ).apply {
                    width = 0
                    height = 36.dp()
                }
                gravity = android.view.Gravity.CENTER
                text = if (inCurrentMonth) dayNum.toString() else ""
                setTextColor(
                    ContextCompat.getColor(
                        this@CalendarPickerActivity,
                        if (inCurrentMonth) R.color.rl_text_primary else R.color.rl_text_muted,
                    ),
                )
                isClickable = inCurrentMonth
                isEnabled = inCurrentMonth
            }
            if (inCurrentMonth) {
                dayViews.add(dayCell)
                dayCell.setOnClickListener {
                    val picked = Calendar.getInstance().apply {
                        set(Calendar.YEAR, displayMonth.get(Calendar.YEAR))
                        set(Calendar.MONTH, displayMonth.get(Calendar.MONTH))
                        set(Calendar.DAY_OF_MONTH, dayNum)
                        set(Calendar.HOUR_OF_DAY, 0)
                        set(Calendar.MINUTE, 0)
                        set(Calendar.SECOND, 0)
                        set(Calendar.MILLISECOND, 0)
                    }
                    selectedDayStartMs = picked.timeInMillis
                    updateHeader()
                    updateDayHighlight()
                }
            }
            grid.addView(dayCell)
        }
        updateDayHighlight()
    }

    private fun updateHeader() {
        val locale = localeForLanguage(appConfig.getAppLanguage())
        val zh = isChineseLanguage(appConfig.getAppLanguage())
        val monthText = SimpleDateFormat("MMMM yyyy", locale).format(displayMonth.time)
        val selectedText = SimpleDateFormat("MMM dd, yyyy", locale).format(Date(selectedDayStartMs))
        findViewById<TextView>(R.id.calendar_month_title).text = monthText
        findViewById<TextView>(R.id.calendar_selected_text).text =
            if (zh) "已选：$selectedText" else "Selected: $selectedText"
    }

    private fun updateDayHighlight() {
        val selectedCal = Calendar.getInstance().apply { timeInMillis = selectedDayStartMs }
        val selectedYear = selectedCal.get(Calendar.YEAR)
        val selectedMonth = selectedCal.get(Calendar.MONTH)
        val selectedDay = selectedCal.get(Calendar.DAY_OF_MONTH)
        val displayYear = displayMonth.get(Calendar.YEAR)
        val displayMonthValue = displayMonth.get(Calendar.MONTH)
        dayViews.forEach { tv ->
            val day = tv.text?.toString()?.trim()?.toIntOrNull()
            val selectedInDisplayedMonth =
                selectedYear == displayYear && selectedMonth == displayMonthValue && day == selectedDay
            if (selectedInDisplayedMonth) {
                tv.setBackgroundResource(R.drawable.bg_calendar_selected)
                tv.setTextColor(ContextCompat.getColor(this, R.color.auth_on_primary))
            } else {
                tv.background = null
                tv.setTextColor(ContextCompat.getColor(this, R.color.rl_text_primary))
            }
        }
    }

    private fun Int.dp(): Int = (this * resources.displayMetrics.density).toInt()

    private fun applyLocalizedTexts(zh: Boolean) {
        findViewById<TextView>(R.id.calendar_page_title).text = if (zh) "选择日期" else "Select Date"
        findViewById<TextView>(R.id.calendar_legend_text).text = if (zh) "有录像数据" else "Has recording data"
        findViewById<TextView>(R.id.calendar_summary_text).text =
            if (zh) "12个事件 · 4小时32分钟录像" else "12 events · 4h 32m recording"
        findViewById<TextView>(R.id.calendar_picker_confirm).text = if (zh) "确认日期" else "Confirm Date"
        findViewById<TextView>(R.id.calendar_picker_cancel).text = if (zh) "取消" else "Cancel"
    }

    private fun isChineseLanguage(languageCode: String?): Boolean {
        if (languageCode.isNullOrBlank()) return false
        return languageCode.lowercase(Locale.US).startsWith("zh")
    }

    private fun localeForLanguage(languageCode: String?): Locale {
        if (languageCode.isNullOrBlank()) return Locale.getDefault()
        return Locale.forLanguageTag(languageCode.replace('_', '-'))
    }

    companion object {
        const val EXTRA_SELECTED_DAY_START_MS = "extra_selected_day_start_ms"
        const val RESULT_SELECTED_DAY_START_MS = "result_selected_day_start_ms"
    }
}
