package com.reallive.android.ui.camera

import android.content.Intent
import android.os.Bundle
import androidx.appcompat.app.AppCompatActivity
import com.google.android.material.button.MaterialButton
import com.reallive.android.R
import com.reallive.android.ui.multiview.MultiViewActivity

class CameraConnectSuccessActivity : AppCompatActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_camera_connect_success)
        findViewById<MaterialButton>(R.id.camera_success_open_live).setOnClickListener {
            startActivity(Intent(this, MultiViewActivity::class.java))
            finish()
        }
        findViewById<MaterialButton>(R.id.camera_success_back_list).setOnClickListener {
            startActivity(Intent(this, CameraListActivity::class.java))
            finish()
        }
    }
}
