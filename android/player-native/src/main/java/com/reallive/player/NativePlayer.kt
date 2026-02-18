package com.reallive.player

import android.util.Log
import android.view.Surface

class NativePlayer : Player {
    private val tag = "RealLiveNativePlayerKt"
    @Volatile
    private var handle: Long = nativeCreate()

    override fun setSurface(surface: Surface?) {
        val h = handle
        if (h == 0L) return
        Log.i(tag, "setSurface handle=$h surface=${surface?.hashCode() ?: "null"}")
        nativeSetSurface(h, surface)
    }

    override fun playLive(url: String) {
        val h = handle
        if (h == 0L) return
        Log.i(tag, "playLive handle=$h url=$url")
        nativePlayLive(h, url)
    }

    override fun playHistory(url: String, startMs: Long) {
        val h = handle
        if (h == 0L) return
        Log.i(tag, "playHistory handle=$h startMs=$startMs url=$url")
        nativePlayHistory(h, url, startMs)
    }

    override fun seek(tsMs: Long) {
        val h = handle
        if (h == 0L) return
        Log.i(tag, "seek handle=$h tsMs=$tsMs")
        nativeSeek(h, tsMs)
    }

    override fun stop() {
        val h = handle
        if (h == 0L) return
        Log.i(tag, "stop handle=$h")
        nativeStop(h)
    }

    override fun getStats(): PlayerStats {
        if (handle == 0L) return PlayerStats()
        val values = nativeGetStats(handle)
        val width = values.getOrNull(0)?.toInt() ?: 0
        val height = values.getOrNull(1)?.toInt() ?: 0
        val decodeFps = values.getOrNull(2) ?: 0.0
        val renderFps = values.getOrNull(3) ?: 0.0
        val bufferedFrames = values.getOrNull(4)?.toLong() ?: 0L
        val stateCode = values.getOrNull(5)?.toInt() ?: 0
        return PlayerStats(
            videoWidth = width,
            videoHeight = height,
            decodeFps = decodeFps,
            renderFps = renderFps,
            bufferedFrames = bufferedFrames,
            state = PlayerState.fromCode(stateCode),
        )
    }

    override fun release() {
        val h = handle
        if (h == 0L) return
        handle = 0L
        Log.i(tag, "release handle=$h")
        nativeRelease(h)
    }

    private external fun nativeCreate(): Long
    private external fun nativeSetSurface(handle: Long, surface: Surface?)
    private external fun nativePlayLive(handle: Long, url: String)
    private external fun nativePlayHistory(handle: Long, url: String, startMs: Long)
    private external fun nativeSeek(handle: Long, tsMs: Long)
    private external fun nativeStop(handle: Long)
    private external fun nativeGetStats(handle: Long): DoubleArray
    private external fun nativeRelease(handle: Long)

    companion object {
        init {
            System.loadLibrary("reallive_player")
        }
    }
}
