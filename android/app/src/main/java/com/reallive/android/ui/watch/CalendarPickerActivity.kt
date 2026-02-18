package com.reallive.android.ui.watch

import android.os.Bundle
import androidx.appcompat.app.AppCompatActivity
import com.google.android.material.appbar.MaterialToolbar
import com.google.android.material.button.MaterialButton
import com.reallive.android.R

class CalendarPickerActivity : AppCompatActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_calendar_picker)
        findViewById<MaterialToolbar>(R.id.calendar_picker_toolbar).setNavigationOnClickListener { finish() }
        findViewById<MaterialButton>(R.id.calendar_picker_confirm).setOnClickListener { finish() }
        findViewById<MaterialButton>(R.id.calendar_picker_cancel).setOnClickListener { finish() }
    }
}
