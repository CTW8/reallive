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

# Core sources
file(GLOB CORE_SOURCES src/*.cpp)

# Platform sources
if(PLATFORM_RPI5)
    file(GLOB PLATFORM_SOURCES platform/rpi5/*.cpp)
endif()

add_executable(reallive-pusher ${CORE_SOURCES} ${PLATFORM_SOURCES})

# Platform-specific dependencies
if(PLATFORM_RPI5)
    # libcamera
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LIBCAMERA libcamera)
    if(LIBCAMERA_FOUND)
        target_include_directories(reallive-pusher PRIVATE ${LIBCAMERA_INCLUDE_DIRS})
        target_link_libraries(reallive-pusher ${LIBCAMERA_LIBRARIES})
    endif()
endif()
