set(BEAST_GODOT_LIBNAME "beast_sdk_native" CACHE STRING "GDExtension library name")
set(BEAST_GODOT_PROJECT_DIR "${CMAKE_SOURCE_DIR}/../godot/beast_sdk_native"
    CACHE PATH "Godot extension manifest directory")

set(GODOT_CPP_ROOT "${CMAKE_SOURCE_DIR}/thirdparty/godot-cpp" CACHE PATH "Path to godot-cpp checkout")

if(NOT EXISTS "${GODOT_CPP_ROOT}/CMakeLists.txt")
    message(FATAL_ERROR
        "godot-cpp not found at ${GODOT_CPP_ROOT}. "
        "Run: sdk/native/tools/setup_godot_cpp.ps1")
endif()

find_package(Python3 3.4 REQUIRED)

add_subdirectory(${GODOT_CPP_ROOT} ${CMAKE_BINARY_DIR}/godot-cpp SYSTEM)

file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/godot-cpp/gen/include")

set(CMAKE_MODULE_PATH "${CMAKE_MODULE_PATH};${GODOT_CPP_ROOT}/cmake")
include(GodotCPPModule)

get_target_property(GODOTCPP_SUFFIX godot::cpp GODOTCPP_SUFFIX)
get_target_property(GODOTCPP_PLATFORM godot::cpp GODOTCPP_PLATFORM)

add_library(${BEAST_GODOT_LIBNAME} SHARED
    ${CMAKE_CURRENT_SOURCE_DIR}/src/register_types.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/register_types.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/beast_config_gd.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/beast_config_gd.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/beast_client_gd.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/beast_client_gd.hpp
)

target_link_libraries(${BEAST_GODOT_LIBNAME} PRIVATE godot::cpp beast_client_core)
target_include_directories(${BEAST_GODOT_LIBNAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

set_target_properties(${BEAST_GODOT_LIBNAME}
    PROPERTIES
    CXX_STANDARD 17
    RUNTIME_OUTPUT_DIRECTORY "$<1:${BEAST_GODOT_PROJECT_DIR}/bin/${GODOTCPP_PLATFORM}>"
    PREFIX ""
    OUTPUT_NAME "${BEAST_GODOT_LIBNAME}${GODOTCPP_SUFFIX}"
)

set(BEAST_GODOT_BINARY_DIR "${BEAST_GODOT_PROJECT_DIR}/bin/${GODOTCPP_PLATFORM}")

add_custom_command(TARGET ${BEAST_GODOT_LIBNAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${BEAST_GODOT_BINARY_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:${BEAST_GODOT_LIBNAME}>" "${BEAST_GODOT_BINARY_DIR}/$<TARGET_FILE_NAME:${BEAST_GODOT_LIBNAME}>"
)
