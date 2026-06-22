cmake_minimum_required(VERSION 3.20)

project(beastserver LANGUAGES CXX)

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
beast_configure_conan_protoc()
beast_find_conan_grpc()

add_subdirectory(platform)
add_subdirectory(plugins)

add_executable(beastserver src/main.cpp)
target_link_libraries(beastserver PRIVATE beast::server beast::core)
target_compile_features(beastserver PRIVATE cxx_std_20)
target_compile_definitions(beast_core PRIVATE BEASTSERVER_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
