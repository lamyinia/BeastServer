# beast_add_plugin(<name>
#   SOURCES <source files...>
#   [PROTO_DIR <bizconfig/protocol/game/...>]  # 与 plugins/game/ 下插件目录对齐
#   [PROTOS <proto files relative to PROTO_DIR...>]
# )
#
# 约定：bizconfig/protocol/game/<pack>/<plugin>/<plugin>.proto
#       ↔ plugins/game/<pack>/<plugin>/ ；生成物在 build/plugins/game/.../<plugin>/generated/
function(beast_add_plugin PLUGIN_NAME)
    set(options "")
    set(one_value PROTO_DIR)
    set(multi_value SOURCES PROTOS)
    cmake_parse_arguments(P "${options}" "${one_value}" "${multi_value}" ${ARGN})

    if(NOT P_SOURCES)
        message(FATAL_ERROR "beast_add_plugin(${PLUGIN_NAME}): SOURCES required")
    endif()

    set(_target "beast_plugin_${PLUGIN_NAME}")
    set(_alias "beast::plugin_${PLUGIN_NAME}")

    set(_proto_target "")
    if(P_PROTOS)
        set(_proto_dir "${CMAKE_CURRENT_SOURCE_DIR}/proto")
        if(P_PROTO_DIR)
            set(_proto_dir "${P_PROTO_DIR}")
        endif()
        set(_gen_dir "${CMAKE_CURRENT_BINARY_DIR}/generated")
        file(MAKE_DIRECTORY "${_gen_dir}")

        set(_proto_srcs "")
        set(_proto_hdrs "")
        set(_proto_deps "")
        foreach(_proto_file IN LISTS P_PROTOS)
            get_filename_component(_proto_name "${_proto_file}" NAME_WE)
            list(APPEND _proto_srcs "${_gen_dir}/${_proto_name}.pb.cc")
            list(APPEND _proto_hdrs "${_gen_dir}/${_proto_name}.pb.h")
            list(APPEND _proto_deps "${_proto_dir}/${_proto_file}")
        endforeach()

        add_custom_command(
            OUTPUT ${_proto_srcs} ${_proto_hdrs}
            COMMAND $<TARGET_FILE:protobuf::protoc>
            ARGS --cpp_out=${_gen_dir} -I ${_proto_dir} ${_proto_deps}
            DEPENDS ${_proto_deps} protobuf::protoc
            COMMENT "Generating ${PLUGIN_NAME} protobuf sources"
            VERBATIM
        )

        set(_proto_target "${_target}_proto")
        add_library(${_proto_target} STATIC ${_proto_srcs} ${_proto_hdrs})
        add_library(${_alias}_proto ALIAS ${_proto_target})
        target_link_libraries(${_proto_target} PUBLIC protobuf::libprotobuf)
        target_include_directories(${_proto_target} PUBLIC "${_gen_dir}")
    endif()

    add_library(${_target} SHARED ${P_SOURCES})
    add_library(${_alias} ALIAS ${_target})
    set_property(GLOBAL APPEND PROPERTY BEAST_PLUGIN_TARGETS "${_target}")

    target_include_directories(${_target} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(${_target} PRIVATE beast::engine)
    target_compile_features(${_target} PRIVATE cxx_std_20)

    if(_proto_target)
        target_link_libraries(${_target} PRIVATE ${_proto_target})
    endif()

    set_target_properties(${_target} PROPERTIES
        PREFIX ""
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins"
    )
endfunction()

# beast_add_biz_protos(<plugin_name> <scheme_dir> [GLOB] [PROTOS <proto files relative to scheme_dir...>])
#
# 为插件生成策划表(biz scheme)protobuf 编译目标。产物:${_target}_biz 静态库,
# 头文件扁平输出到 ${CMAKE_CURRENT_BINARY_DIR}/generated/biz/。
# 调用方需自行 target_link_libraries(<beast_plugin_${plugin_name}> PRIVATE <beast_plugin_${plugin_name}_biz>)。
#
# 用法:
#   beast_add_biz_protos(pixelmoba "${BEAST_BIZ_SCHEME_DIR}/moba/pixel_moba" GLOB)
#   beast_add_biz_protos(demo_tick "${BEAST_BIZ_SCHEME_DIR}/example/npc" PROTOS npc.proto)
function(beast_add_biz_protos PLUGIN_NAME SCHEME_DIR)
    set(options GLOB)
    set(multi_value PROTOS)
    cmake_parse_arguments(P "${options}" "" "${multi_value}" ${ARGN})

    set(_target "beast_plugin_${PLUGIN_NAME}_biz")
    set(_gen_dir "${CMAKE_CURRENT_BINARY_DIR}/generated/biz")
    file(MAKE_DIRECTORY "${_gen_dir}")

    if(P_GLOB)
        file(GLOB _proto_files CONFIGURE_DEPENDS "${SCHEME_DIR}/*.proto")
    else()
        set(_proto_files "")
        foreach(_f IN LISTS P_PROTOS)
            list(APPEND _proto_files "${SCHEME_DIR}/${_f}")
        endforeach()
    endif()

    if(NOT _proto_files)
        message(WARNING "beast_add_biz_protos(${PLUGIN_NAME}): no .proto found in ${SCHEME_DIR}")
        return()
    endif()

    set(_pb_srcs "")
    set(_pb_hdrs "")
    foreach(_p IN LISTS _proto_files)
        get_filename_component(_name "${_p}" NAME_WE)
        list(APPEND _pb_srcs "${_gen_dir}/${_name}.pb.cc")
        list(APPEND _pb_hdrs "${_gen_dir}/${_name}.pb.h")
    endforeach()

    add_custom_command(
        OUTPUT ${_pb_srcs} ${_pb_hdrs}
        COMMAND $<TARGET_FILE:protobuf::protoc>
        ARGS --cpp_out=${_gen_dir} -I ${SCHEME_DIR} ${_proto_files}
        DEPENDS ${_proto_files} protobuf::protoc
        COMMENT "Generating ${PLUGIN_NAME} biz scheme protobuf sources"
        VERBATIM
    )

    add_library(${_target} STATIC ${_pb_srcs} ${_pb_hdrs})
    target_link_libraries(${_target} PUBLIC protobuf::libprotobuf)
    target_include_directories(${_target} PUBLIC "${_gen_dir}")
endfunction()
