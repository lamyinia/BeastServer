# ── etcd proto codegen ──
find_package(Threads REQUIRED)

set(ETCD_PROTO_DIR ${CMAKE_CURRENT_SOURCE_DIR}/proto/etcd)
set(ETCD_GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
file(MAKE_DIRECTORY ${ETCD_GENERATED_DIR}/mvccpb)
file(MAKE_DIRECTORY ${ETCD_GENERATED_DIR}/etcdserverpb)
file(MAKE_DIRECTORY ${ETCD_GENERATED_DIR}/authpb)

set(ETCD_AUTH_PROTO ${ETCD_PROTO_DIR}/authpb/auth.proto)
set(ETCD_KV_PROTO ${ETCD_PROTO_DIR}/mvccpb/kv.proto)
set(ETCD_RPC_PROTO ${ETCD_PROTO_DIR}/etcdserverpb/rpc.proto)

# protobuf C++ 生成(3 个 proto)
add_custom_command(
    OUTPUT ${ETCD_GENERATED_DIR}/authpb/auth.pb.cc ${ETCD_GENERATED_DIR}/authpb/auth.pb.h
           ${ETCD_GENERATED_DIR}/mvccpb/kv.pb.cc ${ETCD_GENERATED_DIR}/mvccpb/kv.pb.h
           ${ETCD_GENERATED_DIR}/etcdserverpb/rpc.pb.cc ${ETCD_GENERATED_DIR}/etcdserverpb/rpc.pb.h
    COMMAND $<TARGET_FILE:protobuf::protoc>
    ARGS --cpp_out=${ETCD_GENERATED_DIR} -I ${ETCD_PROTO_DIR} ${ETCD_AUTH_PROTO} ${ETCD_KV_PROTO} ${ETCD_RPC_PROTO}
    DEPENDS ${ETCD_AUTH_PROTO} ${ETCD_KV_PROTO} ${ETCD_RPC_PROTO} protobuf::protoc
    COMMENT "Generating etcd protobuf sources"
    VERBATIM)

# gRPC stub 生成(仅 rpc.proto)
add_custom_command(
    OUTPUT ${ETCD_GENERATED_DIR}/etcdserverpb/rpc.grpc.pb.cc ${ETCD_GENERATED_DIR}/etcdserverpb/rpc.grpc.pb.h
    COMMAND $<TARGET_FILE:protobuf::protoc>
    ARGS -I ${ETCD_PROTO_DIR}
         --grpc_out=${ETCD_GENERATED_DIR}
         --plugin=protoc-gen-grpc=$<TARGET_FILE:gRPC::grpc_cpp_plugin>
         ${ETCD_RPC_PROTO}
    DEPENDS ${ETCD_RPC_PROTO} protobuf::protoc
    COMMENT "Generating etcd gRPC stubs"
    VERBATIM)

add_library(beast_discovery_proto STATIC
    ${ETCD_GENERATED_DIR}/authpb/auth.pb.cc
    ${ETCD_GENERATED_DIR}/mvccpb/kv.pb.cc
    ${ETCD_GENERATED_DIR}/etcdserverpb/rpc.pb.cc
    ${ETCD_GENERATED_DIR}/etcdserverpb/rpc.grpc.pb.cc)
add_library(beast::discovery_proto ALIAS beast_discovery_proto)
target_link_libraries(beast_discovery_proto PUBLIC protobuf::libprotobuf gRPC::grpc++)
target_include_directories(beast_discovery_proto PUBLIC ${ETCD_GENERATED_DIR})
target_compile_features(beast_discovery_proto PUBLIC cxx_std_20)

# ── discovery 业务库 ──
add_library(beast_discovery STATIC
    src/etcd_client.cpp
    src/service_record.cpp
    src/service_registry.cpp
    src/load_monitor.cpp
    src/etcd_monitor.cpp)
add_library(beast::discovery ALIAS beast_discovery)
target_include_directories(beast_discovery
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_link_libraries(beast_discovery
    PUBLIC beast::discovery_proto beast::core
    PRIVATE Threads::Threads)
target_compile_features(beast_discovery PUBLIC cxx_std_20)
