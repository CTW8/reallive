cmake_minimum_required(VERSION 3.16)
project(reallive-puller LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Platform detection
if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64")
    set(PLATFORM_RPI5 ON)
    message(STATUS "Building for Raspberry Pi 5 (aarch64)")
else()
    # Default to RPi5 build (can be overridden with -DPLATFORM_RPI5=OFF)
    option(PLATFORM_RPI5 "Build with Raspberry Pi 5 platform support" ON)
endif()

# Optional hardware decode support
option(ENABLE_HW_DECODE "Enable V4L2 M2M hardware decoding" ${PLATFORM_RPI5})

# Include directories
include_directories(
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src      # for platform impl headers
)

# Core sources
set(CORE_SOURCES
    src/main.cpp
    src/core/Config.cpp
    src/core/PullPipeline.cpp
)

# Platform sources
set(PLATFORM_SOURCES "")
if(PLATFORM_RPI5)
    list(APPEND PLATFORM_SOURCES
        src/platform/rpi5/FFmpegReceiver.cpp
        src/platform/rpi5/Mp4Storage.cpp
    )

    if(ENABLE_HW_DECODE)
        list(APPEND PLATFORM_SOURCES
            src/platform/rpi5/V4L2Decoder.cpp
        )
        add_definitions(-DENABLE_HW_DECODE)
    endif()
endif()

# Executable
add_executable(reallive-puller ${CORE_SOURCES} ${PLATFORM_SOURCES})

# Find dependencies
find_package(PkgConfig REQUIRED)

# FFmpeg libraries (required)
pkg_check_modules(AVFORMAT REQUIRED libavformat)
pkg_check_modules(AVCODEC REQUIRED libavcodec)
pkg_check_modules(AVUTIL REQUIRED libavutil)

target_include_directories(reallive-puller PRIVATE
    ${AVFORMAT_INCLUDE_DIRS}
    ${AVCODEC_INCLUDE_DIRS}
    ${AVUTIL_INCLUDE_DIRS}
)

target_link_libraries(reallive-puller PRIVATE
    ${AVFORMAT_LIBRARIES}
    ${AVCODEC_LIBRARIES}
    ${AVUTIL_LIBRARIES}
)

# Link pthread (needed for std::thread)
find_package(Threads REQUIRED)
target_link_libraries(reallive-puller PRIVATE Threads::Threads)

# Link filesystem (needed for std::filesystem on some compilers)
target_link_libraries(reallive-puller PRIVATE stdc++fs)

# Install
install(TARGETS reallive-puller RUNTIME DESTINATION bin)
install(FILES config/puller.json DESTINATION etc/reallive)
