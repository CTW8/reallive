cmake_minimum_required(VERSION 3.22)
project(puller_tests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find Google Test
find_package(GTest QUIET)
if(NOT GTest_FOUND)
    include(FetchContent)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG        v1.14.0
    )
    FetchContent_MakeAvailable(googletest)
endif()

# Find nlohmann/json for config parsing tests
find_package(nlohmann_json QUIET)
if(NOT nlohmann_json_FOUND)
    include(FetchContent)
    FetchContent_Declare(
        json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG        v3.11.3
    )
    FetchContent_MakeAvailable(json)
endif()

# Include puller headers
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../include)

# Test executable
add_executable(puller_tests
    test_config.cpp
    test_mock_interfaces.cpp
    test_storage.cpp
)

target_link_libraries(puller_tests
    GTest::gtest
    GTest::gtest_main
    nlohmann_json::nlohmann_json
)

# Register tests with CTest
include(GoogleTest)
gtest_discover_tests(puller_tests)
