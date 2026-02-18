cmake_minimum_required(VERSION 3.22.1)
project(reallive_player)

add_library(
    reallive_player
    SHARED
    jni/NativePlayerJni.cpp
    player/core/PlayerController.cpp
    player/impl/FfmpegPlayer.cpp
)

find_library(log-lib log)
find_library(android-lib android)
find_library(egl-lib EGL)
find_library(glesv2-lib GLESv2)
find_library(z-lib z)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (ANDROID)
    target_link_options(
        reallive_player
        PRIVATE
        "-Wl,-z,max-page-size=16384"
        "-Wl,-z,common-page-size=16384"
    )
endif()

target_include_directories(
    reallive_player
    PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

set(FFMPEG_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/ffmpeg")
set(FFMPEG_LIB_DIR "${FFMPEG_ROOT}/lib/${ANDROID_ABI}")
set(FFMPEG_FOUND OFF)
if (EXISTS "${FFMPEG_ROOT}/include/libavformat/avformat.h" AND EXISTS "${FFMPEG_LIB_DIR}/libavformat.so")
    set(FFMPEG_FOUND ON)
    message(STATUS "Using prebuilt FFmpeg for ${ANDROID_ABI} from ${FFMPEG_LIB_DIR}")
    target_include_directories(
        reallive_player
        PRIVATE
        "${FFMPEG_ROOT}/include"
    )

    add_library(ffmpeg_avformat SHARED IMPORTED)
    set_target_properties(ffmpeg_avformat PROPERTIES IMPORTED_LOCATION "${FFMPEG_LIB_DIR}/libavformat.so")

    add_library(ffmpeg_avcodec SHARED IMPORTED)
    set_target_properties(ffmpeg_avcodec PROPERTIES IMPORTED_LOCATION "${FFMPEG_LIB_DIR}/libavcodec.so")

    add_library(ffmpeg_avutil SHARED IMPORTED)
    set_target_properties(ffmpeg_avutil PROPERTIES IMPORTED_LOCATION "${FFMPEG_LIB_DIR}/libavutil.so")

    add_library(ffmpeg_swscale SHARED IMPORTED)
    set_target_properties(ffmpeg_swscale PROPERTIES IMPORTED_LOCATION "${FFMPEG_LIB_DIR}/libswscale.so")

    add_library(ffmpeg_swresample SHARED IMPORTED)
    set_target_properties(ffmpeg_swresample PROPERTIES IMPORTED_LOCATION "${FFMPEG_LIB_DIR}/libswresample.so")
endif()

target_link_libraries(
    reallive_player
    ${log-lib}
    ${android-lib}
    ${egl-lib}
    ${glesv2-lib}
)

if (FFMPEG_FOUND)
    target_link_libraries(
        reallive_player
        ffmpeg_avformat
        ffmpeg_avcodec
        ffmpeg_avutil
        ffmpeg_swscale
        ffmpeg_swresample
        ${z-lib}
    )
endif()
