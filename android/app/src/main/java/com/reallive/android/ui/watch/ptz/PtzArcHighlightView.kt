package com.reallive.android.ui.watch.ptz

import android.content.Context
import android.graphics.Canvas
import android.graphics.Paint
import android.graphics.RectF
import android.util.AttributeSet
import android.view.View
import kotlin.math.min

class PtzArcHighlightView @JvmOverloads constructor(
    context: Context,
    attrs: AttributeSet? = null,
) : View(context, attrs) {

    private val arcRect = RectF()
    private val paint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.STROKE
        color = 0xB3C8BFFF.toInt()
        strokeCap = Paint.Cap.ROUND
        strokeWidth = dp(12.6f)
    }

    private var startAngle = -45f
    private var sweepAngle = 90f

    fun setArc(startDeg: Float, sweepDeg: Float) {
        startAngle = startDeg
        sweepAngle = sweepDeg
        invalidate()
    }

    override fun onDraw(canvas: Canvas) {
        super.onDraw(canvas)
        val size = min(width.toFloat(), height.toFloat())
        val inset = paint.strokeWidth / 2f + dp(1.2f)
        val left = (width - size) / 2f + inset
        val top = (height - size) / 2f + inset
        val right = left + size - inset * 2f
        val bottom = top + size - inset * 2f
        arcRect.set(left, top, right, bottom)
        canvas.drawArc(arcRect, startAngle, sweepAngle, false, paint)
    }

    private fun dp(value: Float): Float = value * resources.displayMetrics.density
}
