cmake_minimum_required(VERSION 3.20)
project(beast_client_native LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(beast_client_core STATIC
    core/src/wire_codec.cpp
    core/src/frame_codec.cpp
    core/src/message_codec.cpp
    core/src/tcp_transport.cpp
    core/src/io_service.cpp
    core/src/client.cpp
)
target_include_directories(beast_client_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/core/include
)

if(WIN32)
    target_link_libraries(beast_client_core PUBLIC ws2_32)
endif()

option(BEAST_CLIENT_BUILD_C_API "Build beast_client_c shared library" ON)
if(BEAST_CLIENT_BUILD_C_API)
    add_library(beast_client_c SHARED
        bindings/c_api/beast_client_c.cpp
    )
    target_compile_definitions(beast_client_c PRIVATE BEAST_CLIENT_EXPORTS)
    target_link_libraries(beast_client_c PUBLIC beast_client_core)
    target_include_directories(beast_client_c PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/bindings/c_api
    )
endif()

option(BEAST_CLIENT_BUILD_TESTS "Build native unit tests" ON)
if(BEAST_CLIENT_BUILD_TESTS)
    enable_testing()

    add_executable(beast_frame_codec_test tests/frame_codec_test.cpp)
    target_link_libraries(beast_frame_codec_test PRIVATE beast_client_core)
    add_test(NAME beast_frame_codec_test COMMAND beast_frame_codec_test)

    add_executable(beast_message_codec_test tests/message_codec_test.cpp)
    target_link_libraries(beast_message_codec_test PRIVATE beast_client_core)
    add_test(NAME beast_message_codec_test COMMAND beast_message_codec_test)

    add_executable(beast_event_queue_test tests/event_queue_test.cpp)
    target_link_libraries(beast_event_queue_test PRIVATE beast_client_core)
    add_test(NAME beast_event_queue_test COMMAND beast_event_queue_test)
endif()

option(BEAST_CLIENT_BUILD_GODOT "Build Godot GDExtension" OFF)
if(BEAST_CLIENT_BUILD_GODOT)
    add_subdirectory(bindings/godot)
endif()
