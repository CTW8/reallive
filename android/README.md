# RealLive Android

Native Android client skeleton for remote monitoring.

## Modules

- `app`: Android application, Watch prototype page.
- `player-native`: Native HTTP-FLV player with Kotlin interface + JNI + C++ player layers.

## Current status

- Dashboard list is wired to `/api/cameras` with status + thumbnail cards.
- Watch screen is wired to:
  - watch session lifecycle (`start` / `heartbeat` / `stop`)
  - live playback URL assembly (`/live/{stream_key}.flv`)
  - history timeline loading (`overview` + `timeline`)
  - history playback switching (`/history/play?ts=...`)
- Surface rendering pipeline is wired: Kotlin SurfaceView -> JNI -> EGL/GLES.
- Native playback pipeline is wired: `avformat` demux -> `avcodec` decode -> `swscale` RGBA -> GLES texture render.
- Player control APIs are ready (`playLive`, `playHistory`, `seek`, `stop`, `release`).

## Next integration steps

1. Add Kotlin-side player state callbacks (buffering/playing/error) from JNI.
2. Add A/V sync and audio decode path.
3. Tune buffering and reconnect strategy for unstable networks.
4. Evaluate MediaCodec hardware decode path.

## Build prerequisites

- Android Studio (Koala+ recommended)
- Android SDK Platform 34
- Android NDK r26+
- CMake 3.22+

Open `android/` in Android Studio and sync.

## FFmpeg prebuilts

Build Android FFmpeg shared libraries and copy into `player-native`:

```bash
./android/scripts/build_ffmpeg_android.sh
```

Native player layering docs:

- `android/player-native/ARCHITECTURE.md`
