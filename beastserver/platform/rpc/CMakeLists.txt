find_package(GTest REQUIRED)

beast_add_proto_library(rpc_proto
    PROTO_DIR ${BEAST_BIZ_PROTOCOL_PLATFORM_DIR}
    PROTOS room.proto
    GRPC_PROTOS room.proto
    DEPS beast::net_proto gRPC::grpc++ protobuf::libprotobuf)

beast_add_library(rpc
    GLOB src/*.cpp
    DEPS beast::rpc_proto beast::core gRPC::grpc++)

beast_add_gtest(rpc_grpc_server SOURCES tests/grpc_server_test.cpp DEPS beast_rpc)
