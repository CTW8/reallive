# Native Player Architecture

## Layering

1. Kotlin player interface (`Player.kt`)
2. Kotlin JNI implementation (`NativePlayer.kt`)
3. JNI bridge (`src/main/cpp/jni/NativePlayerJni.cpp`)
4. Native player interface + controller (`src/main/cpp/player/interface`, `src/main/cpp/player/core`)
5. Native concrete player (`src/main/cpp/player/impl/FfmpegPlayer.cpp`)

## Data flow

- App depends on Kotlin `Player` interface.
- `NativePlayer` implements `Player` and calls JNI.
- JNI forwards to native `PlayerController` handle.
- `PlayerController` maps commands to native `IPlayer`.
- `FfmpegPlayer` runs decode/render threads and performs FFmpeg + GLES playback.

## Thread model

- Render thread: EGL + GLES texture rendering.
- Decode thread: avformat demux + avcodec decode + swscale RGBA conversion.
- Control path: command updates via atomic flags and mutex-protected state.
