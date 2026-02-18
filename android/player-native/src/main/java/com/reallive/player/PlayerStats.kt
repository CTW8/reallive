package com.reallive.player

enum class PlayerState(val code: Int) {
    IDLE(0),
    CONNECTING(1),
    PLAYING(2),
    BUFFERING(3),
    ENDED(4),
    ERROR(5);

    companion object {
        fun fromCode(code: Int): PlayerState {
            return entries.firstOrNull { it.code == code } ?: IDLE
        }
    }
}

data class PlayerStats(
    val videoWidth: Int = 0,
    val videoHeight: Int = 0,
    val decodeFps: Double = 0.0,
    val renderFps: Double = 0.0,
    val bufferedFrames: Long = 0L,
    val state: PlayerState = PlayerState.IDLE,
)
