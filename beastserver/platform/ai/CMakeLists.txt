find_package(CURL REQUIRED)
find_package(Boost REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(Protobuf CONFIG REQUIRED)

set(_ai_proto_dir "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${_ai_proto_dir}")

set(_ai_proto_src "${_ai_proto_dir}/ai_event.pb.cc")
set(_ai_proto_hdr "${_ai_proto_dir}/ai_event.pb.h")

add_custom_command(
    OUTPUT "${_ai_proto_src}" "${_ai_proto_hdr}"
    COMMAND $<TARGET_FILE:protobuf::protoc>
    ARGS --cpp_out=${_ai_proto_dir}
         -I ${CMAKE_CURRENT_SOURCE_DIR}/proto
         ${CMAKE_CURRENT_SOURCE_DIR}/proto/ai_event.proto
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/proto/ai_event.proto protobuf::protoc
    COMMENT "Generating platform/ai protobuf sources"
    VERBATIM
)

add_library(beast_ai STATIC
    src/model/ai_types.cpp
    src/ai_config.cpp
    src/client/sse_parser.cpp
    src/client/http_client.cpp
    src/client/volc_signer.cpp
    src/driver/ai_driver.cpp
    src/driver/driver_util.cpp
    src/driver/volcengine_driver.cpp
    src/driver/openai_driver.cpp
    src/service/ai_service.cpp
    ${_ai_proto_src}
)

add_library(beast::ai ALIAS beast_ai)

target_include_directories(beast_ai
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${_ai_proto_dir}>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(beast_ai
    PUBLIC
        beast::core
        Boost::boost
        CURL::libcurl
        OpenSSL::Crypto
        protobuf::libprotobuf
)

target_compile_features(beast_ai PUBLIC cxx_std_20)

find_package(GTest REQUIRED)
enable_testing()

add_executable(beast_ai_service_test tests/ai_service_test.cpp)
target_compile_definitions(beast_ai_service_test PRIVATE BEASTSERVER_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_link_libraries(beast_ai_service_test PRIVATE beast_ai beast::core GTest::gtest_main)
add_test(NAME beast_ai_service_test COMMAND beast_ai_service_test)

add_executable(beast_ai_embed_test tests/ai_embed_test.cpp)
target_compile_definitions(beast_ai_embed_test PRIVATE BEASTSERVER_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_link_libraries(beast_ai_embed_test PRIVATE beast_ai beast::core GTest::gtest_main)
add_test(NAME beast_ai_embed_test COMMAND beast_ai_embed_test)
