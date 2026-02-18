package com.reallive.player

import android.content.Context
import android.graphics.SurfaceTexture
import android.graphics.drawable.Drawable
import android.util.Log
import android.util.AttributeSet
import android.view.Surface
import android.view.TextureView

class PlayerSurfaceView @JvmOverloads constructor(
    context: Context,
    attrs: AttributeSet? = null,
    defStyleAttr: Int = 0,
) : TextureView(context, attrs, defStyleAttr), TextureView.SurfaceTextureListener {
    private val tag = "RealLivePlayerView"

    private var player: Player? = null
    private var boundSurface: Surface? = null

    init {
        surfaceTextureListener = this
        isOpaque = true
    }

    override fun setBackground(background: Drawable?) {
        if (background == null) {
            super.setBackground(null)
            return
        }
        Log.w(tag, "Ignoring background drawable on TextureView-backed PlayerSurfaceView")
    }

    @Suppress("DEPRECATION")
    override fun setBackgroundDrawable(background: Drawable?) {
        if (background == null) {
            super.setBackgroundDrawable(null)
            return
        }
        Log.w(tag, "Ignoring setBackgroundDrawable on TextureView-backed PlayerSurfaceView")
    }

    override fun setBackgroundResource(resid: Int) {
        if (resid == 0) {
            super.setBackgroundResource(0)
            return
        }
        Log.w(tag, "Ignoring setBackgroundResource($resid) on TextureView-backed PlayerSurfaceView")
    }

    fun bindPlayer(player: Player) {
        if (this.player === player) return
        Log.i(tag, "bindPlayer view=${hashCode()} player=${player.hashCode()}")
        runCatching { this.player?.setSurface(null) }
        this.player = player
        val s = boundSurface
        if (s != null) {
            Log.i(tag, "bindPlayer attach existing surface view=${hashCode()} surface=${s.hashCode()}")
            runCatching { this.player?.setSurface(s) }
        }
    }

    fun unbindPlayer() {
        val bound = player
        player = null
        Log.i(tag, "unbindPlayer view=${hashCode()} player=${bound?.hashCode() ?: "null"}")
        runCatching { bound?.setSurface(null) }
    }

    override fun onSurfaceTextureAvailable(surfaceTexture: SurfaceTexture, width: Int, height: Int) {
        Log.i(tag, "surfaceAvailable view=${hashCode()} size=${width}x${height} texture=${surfaceTexture.hashCode()}")
        releaseBoundSurface()
        boundSurface = Surface(surfaceTexture)
        val s = boundSurface
        if (s != null) {
            Log.i(tag, "surfaceAvailable bind surface view=${hashCode()} surface=${s.hashCode()}")
            runCatching { player?.setSurface(s) }
        }
    }

    override fun onSurfaceTextureSizeChanged(surfaceTexture: SurfaceTexture, width: Int, height: Int) {
        Log.i(tag, "surfaceSizeChanged view=${hashCode()} size=${width}x${height}")
        val s = boundSurface
        if (s != null) {
            runCatching { player?.setSurface(s) }
        }
    }

    override fun onSurfaceTextureDestroyed(surfaceTexture: SurfaceTexture): Boolean {
        Log.i(tag, "surfaceDestroyed view=${hashCode()} texture=${surfaceTexture.hashCode()}")
        runCatching { player?.setSurface(null) }
        releaseBoundSurface()
        return true
    }

    override fun onSurfaceTextureUpdated(surfaceTexture: SurfaceTexture) = Unit

    private fun releaseBoundSurface() {
        Log.i(tag, "releaseSurface view=${hashCode()} surface=${boundSurface?.hashCode() ?: "null"}")
        boundSurface?.release()
        boundSurface = null
    }
}
