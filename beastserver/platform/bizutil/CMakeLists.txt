find_package(GTest REQUIRED)
find_package(Protobuf CONFIG REQUIRED)

beast_add_library(bizutil
    GLOB src/*.cpp
    DEPS beast::core protobuf::libprotobuf)

set(_bizutil_test_proto_dir "${CMAKE_CURRENT_BINARY_DIR}/test_proto")
beast_generate_protobuf(
    OUT_DIR "${_bizutil_test_proto_dir}"
    PROTO_DIR "${CMAKE_CURRENT_SOURCE_DIR}/tests/proto"
    PROTOS example_table.proto
    OUT_SOURCES _bizutil_test_proto_src
    COMMENT "Generating bizutil test protobuf sources")

beast_add_gtest(bizutil_config
    SOURCES tests/biz_config_store_test.cpp ${_bizutil_test_proto_src}
    DEPS beast_bizutil
    INCLUDE_DIRS "${_bizutil_test_proto_dir}")

beast_add_gtest(bizutil_math
    GLOB tests/math/*.cpp
    DEPS beast_bizutil)
