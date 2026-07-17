cmake_minimum_required(VERSION 3.20)

# 拒绝 in-source build：CMAKE_BINARY_DIR 与 CMAKE_SOURCE_DIR 相同时，
# CMake 会把所有构建产物（CMakeFiles/、*.o、*.so、Makefile 等）散落到源码树，
# 既污染版本控制又占用磁盘（插件 .so 单个可达 100MB+）。
# 根因常为 IDE 把 build path 配成源码目录本身。请改用独立 build 目录
# （如 build/、cmake-build-debug/），再重新 configure。
if(CMAKE_BINARY_DIR STREQUAL CMAKE_SOURCE_DIR)
    message(FATAL_ERROR
        "In-source build detected: CMAKE_BINARY_DIR == CMAKE_SOURCE_DIR (${CMAKE_SOURCE_DIR}).\n"
        "This pollutes the source tree with build artifacts.\n"
        "Fix: set the build directory to a subfolder (e.g. 'build/' or 'cmake-build-debug/') "
        "in CLion: Settings -> Build, Execution, Deployment -> CMake -> Build path,\n"
        "or run cmake with -B <build_dir> from CLI. Then reconfigure.")
endif()

project(beastserver LANGUAGES C CXX)

include(${CMAKE_SOURCE_DIR}/cmake/BeastPaths.cmake)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# 链到源码根目录，clangd / CLion 可直接索引。
if(CMAKE_EXPORT_COMPILE_COMMANDS)
    set(_compile_commands_link "${CMAKE_SOURCE_DIR}/compile_commands.json")
    if(EXISTS "${_compile_commands_link}" AND NOT IS_SYMLINK "${_compile_commands_link}")
        file(REMOVE "${_compile_commands_link}")
    endif()
    if(NOT EXISTS "${_compile_commands_link}")
        file(CREATE_LINK
            "${CMAKE_BINARY_DIR}/compile_commands.json"
            "${_compile_commands_link}"
            SYMBOLIC
        )
    endif()
    message(STATUS "clangd: compile_commands.json -> ${CMAKE_BINARY_DIR}/compile_commands.json")
endif()

enable_testing()

include(${CMAKE_SOURCE_DIR}/cmake/ConanDeps.cmake)
find_package(Protobuf CONFIG REQUIRED)
find_package(GTest REQUIRED)
beast_configure_conan_protoc()
beast_find_conan_grpc()

add_subdirectory(platform)
add_subdirectory(gameplays)

add_executable(beastserver src/main.cpp)
target_link_libraries(beastserver PRIVATE beast::server beast::core)
target_compile_features(beastserver PRIVATE cxx_std_20)

# 平台变更后须同步重编插件；否则 dlopen 可能因 ABI 不一致而挂死。
get_property(_beast_plugin_targets GLOBAL PROPERTY BEAST_PLUGIN_TARGETS)
if(_beast_plugin_targets)
    add_dependencies(beastserver ${_beast_plugin_targets})
endif()
target_compile_definitions(beast_core PRIVATE BEASTSERVER_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

# 定义在 plugins 之后，以便 if(TARGET beast_plugin_demo_event) 为真并 link 其 proto target。
add_executable(beast_server_demo_event_room_integration_test
    ${CMAKE_SOURCE_DIR}/platform/server/tests/demo_event_room_integration_test.cpp)
target_link_libraries(beast_server_demo_event_room_integration_test PRIVATE
    beast::server
    beast::net
    beast::net_proto
    beast::kcp
    GTest::gtest_main)
if(TARGET beast_plugin_demo_event)
    add_dependencies(beast_server_demo_event_room_integration_test beast_plugin_demo_event)
    target_link_libraries(beast_server_demo_event_room_integration_test PRIVATE beast_plugin_demo_event_proto)
    target_include_directories(beast_server_demo_event_room_integration_test PRIVATE
        "${CMAKE_BINARY_DIR}/gameplays/example/demo_event/generated")
endif()
target_compile_definitions(beast_server_demo_event_room_integration_test PRIVATE
    BEASTSERVER_BINARY_DIR="${CMAKE_BINARY_DIR}")
add_test(NAME beast_server_demo_event_room_integration_test COMMAND beast_server_demo_event_room_integration_test)
