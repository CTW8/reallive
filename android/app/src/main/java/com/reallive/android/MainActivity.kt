package com.reallive.android

import android.os.Bundle
import android.widget.Button
import android.widget.EditText
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import com.reallive.player.Player
import com.reallive.player.PlayerFactory
import com.reallive.player.PlayerSurfaceView

class MainActivity : AppCompatActivity() {
    private lateinit var player: Player
    private lateinit var surfaceView: PlayerSurfaceView
    private lateinit var statusText: TextView

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)

        player = PlayerFactory.create()
        surfaceView = findViewById(R.id.player_surface)
        statusText = findViewById(R.id.status_text)
        surfaceView.bindPlayer(player)

        val liveUrlInput = findViewById<EditText>(R.id.live_url_input)
        val historyUrlInput = findViewById<EditText>(R.id.history_url_input)
        val historySeekInput = findViewById<EditText>(R.id.history_seek_input)

        findViewById<Button>(R.id.btn_play_live).setOnClickListener {
            val url = liveUrlInput.text.toString().trim()
            if (url.isNotEmpty()) {
                player.playLive(url)
                statusText.text = "Live playing"
            } else {
                statusText.text = "Input live URL"
            }
        }

        findViewById<Button>(R.id.btn_play_history).setOnClickListener {
            val url = historyUrlInput.text.toString().trim()
            val seekMs = historySeekInput.text.toString().toLongOrNull() ?: 0L
            if (url.isNotEmpty()) {
                player.playHistory(url, seekMs)
                statusText.text = "History playing"
            } else {
                statusText.text = "Input history URL"
            }
        }

        findViewById<Button>(R.id.btn_seek).setOnClickListener {
            val seekMs = historySeekInput.text.toString().toLongOrNull()
            if (seekMs != null) {
                player.seek(seekMs)
                statusText.text = "Seek to $seekMs"
            } else {
                statusText.text = "Input seek ms"
            }
        }

        findViewById<Button>(R.id.btn_stop).setOnClickListener {
            player.stop()
            statusText.text = "Stopped"
        }
    }

    override fun onDestroy() {
        surfaceView.unbindPlayer()
        player.release()
        super.onDestroy()
    }
}
