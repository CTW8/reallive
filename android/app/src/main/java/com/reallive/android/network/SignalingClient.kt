package com.reallive.android.network

import io.socket.client.IO
import io.socket.client.Socket as IoSocket
import io.socket.engineio.client.transports.WebSocket as EngineIoWebSocket
import org.json.JSONObject

class SignalingClient(
    baseUrl: String,
    token: String,
    private val userId: Long,
) {
    private fun JSONObject.optNullableString(key: String): String? {
        if (!has(key) || isNull(key)) return null
        return optString(key).takeIf { it.isNotBlank() }
    }

    interface Listener {
        fun onConnected()
        fun onDisconnected()
        fun onCameraStatus(cameraId: Long, status: String, thumbnailUrl: String?, runtime: DeviceStateDto?)
        fun onActivityEvent(event: ActivityEvent)
    }

    data class ActivityEvent(
        val type: String,
        val cameraId: Long?,
        val cameraName: String?,
        val timestamp: String?,
        val score: Double?,
    )

    private val socket: IoSocket
    private var listener: Listener? = null

    init {
        val options = IO.Options()
        options.path = "/ws/signaling"
        options.transports = arrayOf(EngineIoWebSocket.NAME)
        options.reconnection = true
        options.auth = mapOf("token" to token)
        socket = IO.socket(baseUrl, options)
    }

    fun setListener(listener: Listener?) {
        this.listener = listener
    }

    fun connect() {
        socket.off()

        socket.on(IoSocket.EVENT_CONNECT) {
            socket.emit("join-room", JSONObject().put("room", "dashboard-$userId"))
            listener?.onConnected()
        }

        socket.on(IoSocket.EVENT_DISCONNECT) {
            listener?.onDisconnected()
        }

        socket.on("camera-status") { args ->
            val payload = args.firstOrNull() as? JSONObject ?: return@on
            val cameraId = payload.optLong("cameraId", -1L)
            if (cameraId <= 0L) return@on
            val status = payload.optString("status", "offline")
            val thumbnailUrl = payload.optNullableString("thumbnailUrl")
            val runtimeObj = payload.optJSONObject("runtime")
            val runtime = if (runtimeObj != null) {
                DeviceStateDto(
                    ts = runtimeObj.optLong("ts", 0L).takeIf { it > 0L },
                    running = runtimeObj.optBoolean("running"),
                    desiredLive = runtimeObj.optBoolean("desiredLive"),
                    activeLive = runtimeObj.optBoolean("activeLive"),
                    reason = runtimeObj.optNullableString("reason"),
                    commandSeq = runtimeObj.optLong("commandSeq", -1L).takeIf { it >= 0L },
                    updatedAt = runtimeObj.optLong("updatedAt", 0L).takeIf { it > 0L },
                )
            } else {
                null
            }
            listener?.onCameraStatus(cameraId, status, thumbnailUrl, runtime)
        }

        socket.on("activity-event") { args ->
            val payload = args.firstOrNull() as? JSONObject ?: return@on
            val event = ActivityEvent(
                type = payload.optString("type", "event"),
                cameraId = payload.optLong("cameraId", -1L).takeIf { it > 0L },
                cameraName = payload.optNullableString("cameraName"),
                timestamp = payload.optNullableString("timestamp"),
                score = payload.optDouble("score", Double.NaN).takeIf { !it.isNaN() },
            )
            listener?.onActivityEvent(event)
        }

        socket.connect()
    }

    fun disconnect() {
        socket.off()
        socket.disconnect()
    }
}
