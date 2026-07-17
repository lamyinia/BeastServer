find_package(CURL REQUIRED)
find_package(Boost REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(Protobuf CONFIG REQUIRED)
find_package(GTest REQUIRED)

set(_ai_proto_dir "${CMAKE_CURRENT_BINARY_DIR}/generated")
beast_generate_protobuf(
    OUT_DIR "${_ai_proto_dir}"
    PROTO_DIR "${CMAKE_CURRENT_SOURCE_DIR}/proto"
    PROTOS ai_event.proto
    OUT_SOURCES _ai_proto_src
    COMMENT "Generating platform/ai protobuf sources")

beast_add_library(ai
    GLOB src/*.cpp
    SOURCES ${_ai_proto_src}
    INCLUDE_DIRS "${_ai_proto_dir}"
    DEPS beast::core Boost::boost CURL::libcurl OpenSSL::Crypto protobuf::libprotobuf)

enable_testing()

add_executable(beast_ai_service_test tests/ai_service_test.cpp)
target_compile_definitions(beast_ai_service_test PRIVATE BEASTSERVER_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_link_libraries(beast_ai_service_test PRIVATE beast_ai beast::core GTest::gtest_main)
add_test(NAME beast_ai_service_test COMMAND beast_ai_service_test)

add_executable(beast_ai_embed_test tests/ai_embed_test.cpp)
target_compile_definitions(beast_ai_embed_test PRIVATE BEASTSERVER_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_link_libraries(beast_ai_embed_test PRIVATE beast_ai beast::core GTest::gtest_main)
add_test(NAME beast_ai_embed_test COMMAND beast_ai_embed_test)
