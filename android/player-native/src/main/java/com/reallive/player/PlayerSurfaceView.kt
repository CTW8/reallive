package com.reallive.player

import android.content.Context
import android.util.AttributeSet
import android.view.SurfaceHolder
import android.view.SurfaceView

class PlayerSurfaceView @JvmOverloads constructor(
    context: Context,
    attrs: AttributeSet? = null,
    defStyleAttr: Int = 0,
) : SurfaceView(context, attrs, defStyleAttr), SurfaceHolder.Callback {

    private var player: Player? = null

    init {
        holder.addCallback(this)
    }

    fun bindPlayer(player: Player) {
        this.player = player
        if (holder.surface != null && holder.surface.isValid) {
            this.player?.setSurface(holder.surface)
        }
    }

    fun unbindPlayer() {
        player?.setSurface(null)
        player = null
    }

    override fun surfaceCreated(holder: SurfaceHolder) {
        player?.setSurface(holder.surface)
    }

    override fun surfaceChanged(holder: SurfaceHolder, format: Int, width: Int, height: Int) {
        player?.setSurface(holder.surface)
    }

    override fun surfaceDestroyed(holder: SurfaceHolder) {
        player?.setSurface(null)
    }
}
