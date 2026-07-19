find_package(Boost REQUIRED)
find_package(GTest REQUIRED)
find_package(OpenSSL REQUIRED)
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

beast_add_proto_library(net_proto
    PROTO_DIR ${BEAST_BIZ_PROTOCOL_PLATFORM_DIR}
    PROTOS envelope.proto auth.proto room.proto
    DEPS protobuf::libprotobuf)

beast_add_c_library(kcp
    SOURCES third_party/kcp/ikcp.c
    INCLUDE_DIRS third_party/kcp
    DEPS Threads::Threads)

beast_add_library(net
    GLOB src/*.cpp
    DEPS beast::core beast::net_proto beast::kcp Boost::boost OpenSSL::SSL OpenSSL::Crypto Threads::Threads)

beast_add_gtest(net_pipeline SOURCES tests/channel_pipeline_test.cpp DEPS beast_net)
beast_add_gtest(net_outbound SOURCES tests/outbound_hub_test.cpp DEPS beast_net)
beast_add_gtest(net_length_field SOURCES tests/length_field_codec_test.cpp DEPS beast_net)
beast_add_gtest(net_protobuf_codec SOURCES tests/protobuf_codec_test.cpp DEPS beast_net)
beast_add_gtest(net_tcp_loopback SOURCES tests/tcp_loopback_test.cpp DEPS beast_net)
beast_add_gtest(net_websocket_loopback SOURCES tests/websocket_loopback_test.cpp DEPS beast_net)
beast_add_gtest(net_ssl_loopback SOURCES tests/ssl_loopback_test.cpp DEPS beast_net)
beast_add_gtest(net_kcp_loopback SOURCES tests/kcp_loopback_test.cpp DEPS beast_net)
beast_add_gtest(net_unreliable_frame SOURCES tests/unreliable_frame_test.cpp DEPS beast_net)
beast_add_gtest(net_kcp_crypto SOURCES tests/kcp_crypto_test.cpp DEPS beast_net)
beast_add_gtest(net_kcp_crypto_loopback SOURCES tests/kcp_crypto_loopback_test.cpp DEPS beast_net)
beast_add_gtest(net_kcp_bypass_crypto_loopback SOURCES tests/kcp_bypass_crypto_loopback_test.cpp DEPS beast_net)
beast_add_gtest(net_dtls_loopback SOURCES tests/dtls_loopback_test.cpp DEPS beast_net)
beast_add_gtest(net_session_instance SOURCES tests/session_instance_test.cpp DEPS beast_net)
beast_add_gtest(net_session_concurrent SOURCES tests/session_manager_concurrent_test.cpp DEPS beast_net)
beast_add_gtest(net_auth_verifier SOURCES tests/auth_verifier_test.cpp DEPS beast_net)
