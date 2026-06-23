find_package(Boost REQUIRED)
find_package(GTest REQUIRED)
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

set(BEAST_PROTO_DIR ${BEAST_BIZ_PROTOCOL_PLATFORM_DIR})
set(BEAST_GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
file(MAKE_DIRECTORY ${BEAST_GENERATED_DIR})

set(BEAST_PROTO_FILES
    ${BEAST_PROTO_DIR}/envelope.proto
    ${BEAST_PROTO_DIR}/auth.proto
    ${BEAST_PROTO_DIR}/room.proto
)

set(BEAST_PROTO_PB_SRCS)
set(BEAST_PROTO_PB_HDRS)
foreach(_proto_file ${BEAST_PROTO_FILES})
    get_filename_component(_proto_name "${_proto_file}" NAME_WE)
    list(APPEND BEAST_PROTO_PB_SRCS "${BEAST_GENERATED_DIR}/${_proto_name}.pb.cc")
    list(APPEND BEAST_PROTO_PB_HDRS "${BEAST_GENERATED_DIR}/${_proto_name}.pb.h")
endforeach()

add_custom_command(
    OUTPUT ${BEAST_PROTO_PB_SRCS} ${BEAST_PROTO_PB_HDRS}
    COMMAND $<TARGET_FILE:protobuf::protoc>
    ARGS --cpp_out=${BEAST_GENERATED_DIR} -I ${BEAST_PROTO_DIR} ${BEAST_PROTO_FILES}
    DEPENDS ${BEAST_PROTO_FILES} protobuf::protoc
    COMMENT "Generating beast.net protobuf sources"
    VERBATIM
)

add_library(beast_net_proto STATIC ${BEAST_PROTO_PB_SRCS} ${BEAST_PROTO_PB_HDRS})
add_library(beast::net_proto ALIAS beast_net_proto)
target_link_libraries(beast_net_proto PUBLIC protobuf::libprotobuf)
target_include_directories(beast_net_proto PUBLIC ${BEAST_GENERATED_DIR})

add_library(beast_net STATIC
    src/channel/channel_pipeline.cpp
    src/channel/tcp_channel.cpp
    src/channel/codec/length_field_decoder.cpp
    src/channel/codec/length_field_encoder.cpp
    src/channel/codec/protobuf_decoder.cpp
    src/channel/codec/protobuf_encoder.cpp
    src/channel/tcp_pipeline.cpp
    src/auth/auth_verifier.cpp
    src/auth/auth_handler.cpp
    src/dispatch/router.cpp
    src/dispatch/router_handler.cpp
    src/channel/transport/tcp_transport.cpp
    src/session/session.cpp
    src/session/session_manager.cpp
    src/outbound/outbound_hub.cpp
    src/listener/tcp_listener.cpp
    src/io/io_context_runner.cpp
    src/server/tcp_server.cpp
)

add_library(beast::net ALIAS beast_net)

target_include_directories(beast_net
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(beast_net
    PUBLIC
        beast::core
        beast::net_proto
        Boost::boost
        Threads::Threads
)

target_compile_features(beast_net PUBLIC cxx_std_20)

enable_testing()

add_executable(beast_net_pipeline_test tests/channel_pipeline_test.cpp)
target_link_libraries(beast_net_pipeline_test PRIVATE beast_net GTest::gtest_main)
add_test(NAME beast_net_pipeline_test COMMAND beast_net_pipeline_test)

add_executable(beast_net_outbound_test tests/outbound_hub_test.cpp)
target_link_libraries(beast_net_outbound_test PRIVATE beast_net GTest::gtest_main)
add_test(NAME beast_net_outbound_test COMMAND beast_net_outbound_test)

add_executable(beast_net_length_field_test tests/length_field_codec_test.cpp)
target_link_libraries(beast_net_length_field_test PRIVATE beast_net GTest::gtest_main)
add_test(NAME beast_net_length_field_test COMMAND beast_net_length_field_test)

add_executable(beast_net_protobuf_codec_test tests/protobuf_codec_test.cpp)
target_link_libraries(beast_net_protobuf_codec_test PRIVATE beast_net GTest::gtest_main)
add_test(NAME beast_net_protobuf_codec_test COMMAND beast_net_protobuf_codec_test)

add_executable(beast_net_tcp_loopback_test tests/tcp_loopback_test.cpp)
target_link_libraries(beast_net_tcp_loopback_test PRIVATE beast_net GTest::gtest_main)
add_test(NAME beast_net_tcp_loopback_test COMMAND beast_net_tcp_loopback_test)

add_executable(beast_net_session_instance_test tests/session_instance_test.cpp)
target_link_libraries(beast_net_session_instance_test PRIVATE beast_net GTest::gtest_main)
add_test(NAME beast_net_session_instance_test COMMAND beast_net_session_instance_test)

add_executable(beast_net_session_concurrent_test tests/session_manager_concurrent_test.cpp)
target_link_libraries(beast_net_session_concurrent_test PRIVATE beast_net GTest::gtest_main)
add_test(NAME beast_net_session_concurrent_test COMMAND beast_net_session_concurrent_test)
