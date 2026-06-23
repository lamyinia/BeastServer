# beast_add_plugin(<name>
#   SOURCES <source files...>
#   [PROTOS <proto files relative to plugin dir...>]
# )
#
# 约定：每个玩法一个子目录 plugins/<name>/CMakeLists.txt，根 CMake 自动 add_subdirectory。
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
