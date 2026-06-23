find_package(GTest REQUIRED)

set(BEAST_RPC_PROTO_DIR ${BEAST_BIZ_PROTOCOL_PLATFORM_DIR})
set(BEAST_RPC_GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
file(MAKE_DIRECTORY ${BEAST_RPC_GENERATED_DIR})

set(BEAST_ROOM_PROTO ${BEAST_RPC_PROTO_DIR}/room.proto)
set(BEAST_ROOM_GRPC_SRC ${BEAST_RPC_GENERATED_DIR}/room.grpc.pb.cc)
set(BEAST_ROOM_GRPC_HDR ${BEAST_RPC_GENERATED_DIR}/room.grpc.pb.h)

add_custom_command(
    OUTPUT ${BEAST_ROOM_GRPC_SRC} ${BEAST_ROOM_GRPC_HDR}
    COMMAND $<TARGET_FILE:protobuf::protoc>
    ARGS
        -I ${BEAST_RPC_PROTO_DIR}
        --grpc_out=${BEAST_RPC_GENERATED_DIR}
        --plugin=protoc-gen-grpc=$<TARGET_FILE:gRPC::grpc_cpp_plugin>
        ${BEAST_ROOM_PROTO}
    DEPENDS ${BEAST_ROOM_PROTO} protobuf::protoc
    COMMENT "Generating gRPC sources for room.proto"
    VERBATIM
)

add_library(beast_rpc_proto STATIC ${BEAST_ROOM_GRPC_SRC} ${BEAST_ROOM_GRPC_HDR})
add_library(beast::rpc_proto ALIAS beast_rpc_proto)

target_link_libraries(beast_rpc_proto
    PUBLIC
        beast::net_proto
        gRPC::grpc++
        protobuf::libprotobuf
)
target_include_directories(beast_rpc_proto PUBLIC ${BEAST_RPC_GENERATED_DIR})
target_compile_features(beast_rpc_proto PUBLIC cxx_std_20)

add_library(beast_rpc STATIC
    src/grpc_server.cpp
)
add_library(beast::rpc ALIAS beast_rpc)

target_include_directories(beast_rpc
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(beast_rpc
    PUBLIC
        beast::rpc_proto
        beast::core
        gRPC::grpc++
)

target_compile_features(beast_rpc PUBLIC cxx_std_20)

enable_testing()

add_executable(beast_rpc_grpc_server_test tests/grpc_server_test.cpp)
target_link_libraries(beast_rpc_grpc_server_test PRIVATE beast_rpc GTest::gtest_main)
add_test(NAME beast_rpc_grpc_server_test COMMAND beast_rpc_grpc_server_test)
