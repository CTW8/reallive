cmake_minimum_required(VERSION 3.16)
project(reallive-pusher LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Platform detection
if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64")
    set(PLATFORM_RPI5 ON)
    message(STATUS "Building for Raspberry Pi 5 (aarch64)")
endif()

include_directories(${CMAKE_SOURCE_DIR}/include)
include_directories(${CMAKE_SOURCE_DIR}/src)

option(REALLIVE_ENABLE_OPENCV "Enable OpenCV motion detector for person detection" ON)
option(REALLIVE_ENABLE_TFLITE "Enable TFLite SSD detector for person detection" ON)

# Core sources
set(CORE_SOURCES
    src/main.cpp
    src/core/Config.cpp
    src/core/Pipeline.cpp
    src/core/LocalRecorder.cpp
    src/core/ControlServer.cpp
)

# Platform sources
if(PLATFORM_RPI5)
    set(PLATFORM_SOURCES
        src/platform/rpi5/LibcameraCapture.cpp
        src/platform/rpi5/AvcodecEncoder.cpp
        src/platform/rpi5/AlsaCapture.cpp
        src/platform/rpi5/RtmpStreamer.cpp
    )
    # Note: V4L2 hardware encoder removed - Pi 5 does not have hardware H.264 encoder
endif()

add_executable(reallive-pusher ${CORE_SOURCES} ${PLATFORM_SOURCES})

# Platform-specific dependencies
if(PLATFORM_RPI5)
    find_package(PkgConfig REQUIRED)

    # libcamera
    pkg_check_modules(LIBCAMERA libcamera)
    if(LIBCAMERA_FOUND)
        target_include_directories(reallive-pusher PRIVATE ${LIBCAMERA_INCLUDE_DIRS})
        target_link_libraries(reallive-pusher ${LIBCAMERA_LIBRARIES})
    endif()

    # FFmpeg (libavformat, libavcodec, libavutil for RTMP streaming)
    pkg_check_modules(AVFORMAT libavformat)
    pkg_check_modules(AVCODEC libavcodec)
    pkg_check_modules(AVUTIL libavutil)
    if(AVFORMAT_FOUND AND AVCODEC_FOUND AND AVUTIL_FOUND)
        target_include_directories(reallive-pusher PRIVATE
            ${AVFORMAT_INCLUDE_DIRS}
            ${AVCODEC_INCLUDE_DIRS}
            ${AVUTIL_INCLUDE_DIRS}
        )
        target_link_libraries(reallive-pusher
            ${AVFORMAT_LIBRARIES}
            ${AVCODEC_LIBRARIES}
            ${AVUTIL_LIBRARIES}
        )
    endif()

    # ALSA (libasound for audio capture)
    pkg_check_modules(ALSA alsa)
    if(ALSA_FOUND)
        target_include_directories(reallive-pusher PRIVATE ${ALSA_INCLUDE_DIRS})
        target_link_libraries(reallive-pusher ${ALSA_LIBRARIES})
    endif()

    if(REALLIVE_ENABLE_OPENCV)
        find_package(OpenCV QUIET COMPONENTS core imgproc)
        if(OpenCV_FOUND)
            target_include_directories(reallive-pusher PRIVATE ${OpenCV_INCLUDE_DIRS})
            target_link_libraries(reallive-pusher ${OpenCV_LIBS})
            target_compile_definitions(reallive-pusher PRIVATE REALLIVE_HAS_OPENCV=1)
            message(STATUS "OpenCV motion detector enabled")
        else()
            message(WARNING "OpenCV not found, falling back to non-OpenCV motion detector")
        endif()
    endif()

    if(REALLIVE_ENABLE_TFLITE)
        find_path(TFLITE_INCLUDE_DIR tensorflow/lite/interpreter.h)
        find_library(TFLITE_LIBRARY tensorflow-lite)
        if(TFLITE_INCLUDE_DIR AND TFLITE_LIBRARY)
            target_include_directories(reallive-pusher PRIVATE ${TFLITE_INCLUDE_DIR})
            target_link_libraries(reallive-pusher ${TFLITE_LIBRARY})
            target_compile_definitions(reallive-pusher PRIVATE REALLIVE_HAS_TFLITE=1)
            message(STATUS "TFLite SSD detector enabled")
        else()
            message(WARNING "TFLite not found, person detection will stay in motion-only fallback mode")
        endif()
    endif()
endif()
