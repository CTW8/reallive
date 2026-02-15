package com.reallive.player

import android.view.Surface

interface Player {
    fun setSurface(surface: Surface?)

    fun playLive(url: String)

    fun playHistory(url: String, startMs: Long = 0L)

    fun seek(tsMs: Long)

    fun stop()

    fun release()
}
