# FFmpeg prebuilts for Android

This directory stores FFmpeg shared libraries and headers used by `player-native`.

## Build script

Run from repository root:

```bash
./android/scripts/build_ffmpeg_android.sh
```

The script will:

1. Detect Android NDK (or use `ANDROID_NDK_HOME`)
2. Build FFmpeg from `ffmpeg-8.0.1` for:
   - `arm64-v8a`
   - `armeabi-v7a`
3. Copy outputs to:
   - `include/`
   - `lib/arm64-v8a/`
   - `lib/armeabi-v7a/`

## Source path

Default FFmpeg source path is:

`<repo>/ffmpeg-8.0.1`

You can override it:

```bash
FFMPEG_SRC=/your/path/ffmpeg-8.0.1 ./android/scripts/build_ffmpeg_android.sh
```

## Notes

- Built as **shared libraries** (`.so`)
- Enabled low-latency relevant components:
  - protocols: `file,http,tcp`
  - demuxers: `flv,mov,mp4,m4a,matroska,mpegts`
  - parsers: `h264,hevc,aac`
  - decoders: `h264,hevc,aac`
