set(_biz_proto "${BEAST_BIZ_SCHEME_DIR}/example/npc/npc.proto")
set(_biz_gen_dir "${CMAKE_CURRENT_BINARY_DIR}/generated/biz")
file(MAKE_DIRECTORY "${_biz_gen_dir}")

set(_npc_pb_src "${_biz_gen_dir}/npc.pb.cc")
set(_npc_pb_hdr "${_biz_gen_dir}/npc.pb.h")

add_custom_command(
    OUTPUT "${_npc_pb_src}" "${_npc_pb_hdr}"
    COMMAND $<TARGET_FILE:protobuf::protoc>
    ARGS --cpp_out=${_biz_gen_dir}
         -I ${BEAST_BIZ_SCHEME_DIR}/example/npc
         ${_biz_proto}
    DEPENDS ${_biz_proto} protobuf::protoc
    COMMENT "Generating demo_tick bizconfig protobuf sources"
    VERBATIM
)

add_library(beast_plugin_demo_tick_biz STATIC "${_npc_pb_src}")
target_link_libraries(beast_plugin_demo_tick_biz PUBLIC protobuf::libprotobuf)
target_include_directories(beast_plugin_demo_tick_biz PUBLIC "${_biz_gen_dir}")

beast_add_plugin(demo_tick
    SOURCES
        plugin.cpp
        engine/demo_tick_engine.cpp
)

target_link_libraries(beast_plugin_demo_tick PRIVATE beast_plugin_demo_tick_biz)
