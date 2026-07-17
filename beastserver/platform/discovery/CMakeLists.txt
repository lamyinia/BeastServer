find_package(Threads REQUIRED)

beast_add_proto_library(discovery_proto
    PROTO_DIR "${CMAKE_CURRENT_SOURCE_DIR}/proto/etcd"
    PROTOS authpb/auth.proto mvccpb/kv.proto etcdserverpb/rpc.proto
    GRPC_PROTOS etcdserverpb/rpc.proto
    DEPS protobuf::libprotobuf gRPC::grpc++)

beast_add_library(discovery
    GLOB src/*.cpp
    DEPS beast::discovery_proto beast::core
    PRIVATE_DEPS Threads::Threads)
