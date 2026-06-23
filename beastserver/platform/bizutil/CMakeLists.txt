find_package(GTest REQUIRED)
find_package(Protobuf CONFIG REQUIRED)

add_library(beast_bizutil STATIC
    src/config/loader.cpp
    src/config/manifest.cpp
    src/config/paths.cpp
    src/config/result.cpp
    src/config/store.cpp
)

add_library(beast::bizutil ALIAS beast_bizutil)

target_include_directories(beast_bizutil
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(beast_bizutil
    PUBLIC
        beast::core
        protobuf::libprotobuf
)

target_compile_features(beast_bizutil PUBLIC cxx_std_20)

enable_testing()

set(_bizutil_test_proto_dir "${CMAKE_CURRENT_BINARY_DIR}/test_proto")
file(MAKE_DIRECTORY "${_bizutil_test_proto_dir}")

set(_bizutil_test_proto_src "${_bizutil_test_proto_dir}/example_table.pb.cc")
set(_bizutil_test_proto_hdr "${_bizutil_test_proto_dir}/example_table.pb.h")

add_custom_command(
    OUTPUT "${_bizutil_test_proto_src}" "${_bizutil_test_proto_hdr}"
    COMMAND $<TARGET_FILE:protobuf::protoc>
    ARGS --cpp_out=${_bizutil_test_proto_dir}
         -I ${CMAKE_CURRENT_SOURCE_DIR}/tests/proto
         ${CMAKE_CURRENT_SOURCE_DIR}/tests/proto/example_table.proto
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tests/proto/example_table.proto protobuf::protoc
    COMMENT "Generating bizutil test protobuf sources"
    VERBATIM
)

add_executable(beast_bizutil_config_test tests/biz_config_store_test.cpp ${_bizutil_test_proto_src})
target_include_directories(beast_bizutil_config_test PRIVATE "${_bizutil_test_proto_dir}")
target_link_libraries(beast_bizutil_config_test PRIVATE beast_bizutil GTest::gtest_main)
add_test(NAME beast_bizutil_config_test COMMAND beast_bizutil_config_test)

add_executable(beast_bizutil_math_test tests/math_clamp_test.cpp)
target_link_libraries(beast_bizutil_math_test PRIVATE beast_bizutil GTest::gtest_main)
add_test(NAME beast_bizutil_math_test COMMAND beast_bizutil_math_test)
