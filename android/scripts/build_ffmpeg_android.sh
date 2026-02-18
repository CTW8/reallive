#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
REPO_ROOT=$(cd "$SCRIPT_DIR/../.." && pwd)

FFMPEG_SRC_DEFAULT="$REPO_ROOT/ffmpeg-8.0.1"
FFMPEG_SRC=${FFMPEG_SRC:-${1:-$FFMPEG_SRC_DEFAULT}}
API_LEVEL=${API_LEVEL:-26}
ABIS_STR=${ABIS:-"arm64-v8a armeabi-v7a"}
ABIS=($ABIS_STR)
PAGE_SIZE_LDFLAGS=${PAGE_SIZE_LDFLAGS:-"-Wl,-z,max-page-size=16384 -Wl,-z,common-page-size=16384"}

OUTPUT_ROOT="$REPO_ROOT/android/player-native/src/main/cpp/third_party/ffmpeg"
BUILD_ROOT="$REPO_ROOT/android/.ffmpeg-build"
INSTALL_ROOT="$BUILD_ROOT/install"

log() {
  printf '[ffmpeg-android] %s\n' "$*"
}

pick_latest_dir() {
  local base="$1"
  if [[ ! -d "$base" ]]; then
    return 1
  fi
  ls -1 "$base" | sort -V | tail -n 1
}

detect_ndk() {
  if [[ -n "${ANDROID_NDK_HOME:-}" && -d "${ANDROID_NDK_HOME}" ]]; then
    echo "$ANDROID_NDK_HOME"
    return 0
  fi
  if [[ -n "${ANDROID_NDK_ROOT:-}" && -d "${ANDROID_NDK_ROOT}" ]]; then
    echo "$ANDROID_NDK_ROOT"
    return 0
  fi

  local sdk_candidates=(
    "${ANDROID_SDK_ROOT:-}"
    "${ANDROID_HOME:-}"
    "$HOME/Library/Android/sdk"
    "$HOME/Android/Sdk"
  )

  local sdk
  for sdk in "${sdk_candidates[@]}"; do
    [[ -n "$sdk" ]] || continue
    local ndk_base="$sdk/ndk"
    if [[ -d "$ndk_base" ]]; then
      local latest
      latest=$(pick_latest_dir "$ndk_base" || true)
      if [[ -n "$latest" && -d "$ndk_base/$latest" ]]; then
        echo "$ndk_base/$latest"
        return 0
      fi
    fi
  done

  return 1
}

host_tag() {
  local uname_s
  uname_s=$(uname -s)
  case "$uname_s" in
    Darwin) echo "darwin-x86_64" ;;
    Linux) echo "linux-x86_64" ;;
    *)
      log "Unsupported host: $uname_s"
      exit 1
      ;;
  esac
}

if [[ ! -d "$FFMPEG_SRC" ]]; then
  log "FFmpeg source not found: $FFMPEG_SRC"
  exit 1
fi

NDK_PATH=$(detect_ndk || true)
if [[ -z "$NDK_PATH" ]]; then
  log "Android NDK not found. Set ANDROID_NDK_HOME manually."
  exit 1
fi

HOST_TAG=$(host_tag)
TOOLCHAIN="$NDK_PATH/toolchains/llvm/prebuilt/$HOST_TAG"
if [[ ! -d "$TOOLCHAIN" ]]; then
  log "NDK toolchain not found: $TOOLCHAIN"
  exit 1
fi

mkdir -p "$BUILD_ROOT" "$INSTALL_ROOT" "$OUTPUT_ROOT"

log "FFmpeg source: $FFMPEG_SRC"
log "NDK: $NDK_PATH"
log "Toolchain: $TOOLCHAIN"
log "ABIs: ${ABIS[*]}"
log "API level: $API_LEVEL"
log "Page-size ldflags: $PAGE_SIZE_LDFLAGS"

for ABI in "${ABIS[@]}"; do
  case "$ABI" in
    arm64-v8a)
      ARCH="aarch64"
      TARGET_HOST="aarch64-linux-android"
      CC="$TOOLCHAIN/bin/${TARGET_HOST}${API_LEVEL}-clang"
      CXX="$TOOLCHAIN/bin/${TARGET_HOST}${API_LEVEL}-clang++"
      EXTRA_CFLAGS="-O3 -fPIC"
      ;;
    armeabi-v7a)
      ARCH="arm"
      TARGET_HOST="armv7a-linux-androideabi"
      CC="$TOOLCHAIN/bin/${TARGET_HOST}${API_LEVEL}-clang"
      CXX="$TOOLCHAIN/bin/${TARGET_HOST}${API_LEVEL}-clang++"
      EXTRA_CFLAGS="-O3 -fPIC -march=armv7-a -mfpu=neon -mfloat-abi=softfp"
      ;;
    *)
      log "Unsupported ABI: $ABI"
      exit 1
      ;;
  esac

  AR="$TOOLCHAIN/bin/llvm-ar"
  NM="$TOOLCHAIN/bin/llvm-nm"
  RANLIB="$TOOLCHAIN/bin/llvm-ranlib"
  STRIP="$TOOLCHAIN/bin/llvm-strip"

  BUILD_DIR="$BUILD_ROOT/$ABI"
  PREFIX="$INSTALL_ROOT/$ABI"

  rm -rf "$BUILD_DIR" "$PREFIX"
  mkdir -p "$BUILD_DIR" "$PREFIX"

  log "Configuring $ABI ..."
  pushd "$BUILD_DIR" >/dev/null

  "$FFMPEG_SRC/configure" \
    --prefix="$PREFIX" \
    --target-os=android \
    --arch="$ARCH" \
    --enable-cross-compile \
    --cc="$CC" \
    --cxx="$CXX" \
    --ar="$AR" \
    --nm="$NM" \
    --ranlib="$RANLIB" \
    --strip="$STRIP" \
    --enable-pic \
    --disable-static \
    --enable-shared \
    --disable-programs \
    --disable-doc \
    --disable-debug \
    --disable-avdevice \
    --disable-avfilter \
    --disable-everything \
    --enable-avcodec \
    --enable-avformat \
    --enable-avutil \
    --enable-swscale \
    --enable-swresample \
    --enable-protocol=file,http,tcp \
    --enable-demuxer=flv,mov,mp4,m4a,matroska,mpegts \
    --enable-parser=h264,hevc,aac \
    --enable-bsf=h264_mp4toannexb,hevc_mp4toannexb,aac_adtstoasc \
    --enable-decoder=h264,hevc,aac \
    --enable-network \
    --extra-cflags="$EXTRA_CFLAGS" \
    --extra-ldflags="-Wl,--gc-sections $PAGE_SIZE_LDFLAGS"

  log "Building $ABI ..."
  make -j"$(sysctl -n hw.logicalcpu 2>/dev/null || echo 8)"
  make install
  popd >/dev/null

done

log "Copying headers and libraries to Android project ..."
rm -rf "$OUTPUT_ROOT/include"
mkdir -p "$OUTPUT_ROOT/include"
cp -R "$INSTALL_ROOT/arm64-v8a/include/." "$OUTPUT_ROOT/include/"

mkdir -p "$OUTPUT_ROOT/lib"
for ABI in "${ABIS[@]}"; do
  rm -rf "$OUTPUT_ROOT/lib/$ABI"
  mkdir -p "$OUTPUT_ROOT/lib/$ABI"
  cp -L "$INSTALL_ROOT/$ABI/lib/libavcodec.so" "$OUTPUT_ROOT/lib/$ABI/"
  cp -L "$INSTALL_ROOT/$ABI/lib/libavformat.so" "$OUTPUT_ROOT/lib/$ABI/"
  cp -L "$INSTALL_ROOT/$ABI/lib/libavutil.so" "$OUTPUT_ROOT/lib/$ABI/"
  cp -L "$INSTALL_ROOT/$ABI/lib/libswscale.so" "$OUTPUT_ROOT/lib/$ABI/"
  cp -L "$INSTALL_ROOT/$ABI/lib/libswresample.so" "$OUTPUT_ROOT/lib/$ABI/"

done

cp "$FFMPEG_SRC/COPYING.LGPLv2.1" "$OUTPUT_ROOT/"
cp "$FFMPEG_SRC/LICENSE.md" "$OUTPUT_ROOT/" 2>/dev/null || true

log "Done. Output: $OUTPUT_ROOT"
