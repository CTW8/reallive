package com.reallive.player

import android.view.Surface

class NativePlayer : Player {
    private var handle: Long = nativeCreate()

    override fun setSurface(surface: Surface?) {
        ensureHandle()
        nativeSetSurface(handle, surface)
    }

    override fun playLive(url: String) {
        ensureHandle()
        nativePlayLive(handle, url)
    }

    override fun playHistory(url: String, startMs: Long) {
        ensureHandle()
        nativePlayHistory(handle, url, startMs)
    }

    override fun seek(tsMs: Long) {
        ensureHandle()
        nativeSeek(handle, tsMs)
    }

    override fun stop() {
        if (handle == 0L) return
        nativeStop(handle)
    }

    override fun release() {
        if (handle == 0L) return
        nativeRelease(handle)
        handle = 0L
    }

    private fun ensureHandle() {
        check(handle != 0L) { "NativePlayer already released" }
    }

    private external fun nativeCreate(): Long
    private external fun nativeSetSurface(handle: Long, surface: Surface?)
    private external fun nativePlayLive(handle: Long, url: String)
    private external fun nativePlayHistory(handle: Long, url: String, startMs: Long)
    private external fun nativeSeek(handle: Long, tsMs: Long)
    private external fun nativeStop(handle: Long)
    private external fun nativeRelease(handle: Long)

    companion object {
        init {
            System.loadLibrary("reallive_player")
        }
    }
}
