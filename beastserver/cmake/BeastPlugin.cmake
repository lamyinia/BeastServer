include(${CMAKE_SOURCE_DIR}/cmake/BeastTargets.cmake)

# Collect proto file names (relative to proto_dir) from explicit list or PROTO_GLOB.
function(_beast_collect_proto_names out_var proto_dir)
    set(options PROTO_GLOB)
    set(one_value "")
    set(multi_value PROTOS)
    cmake_parse_arguments(_CP "${options}" "${one_value}" "${multi_value}" ${ARGN})

    set(_names ${_CP_PROTOS})
    if(_CP_PROTO_GLOB)
        file(GLOB _proto_files CONFIGURE_DEPENDS "${proto_dir}/*.proto")
        foreach(_file IN LISTS _proto_files)
            get_filename_component(_name "${_file}" NAME)
            list(APPEND _names "${_name}")
        endforeach()
    endif()
    if(_names)
        list(SORT _names)
        list(REMOVE_DUPLICATES _names)
    endif()
    set(${out_var} "${_names}" PARENT_SCOPE)
endfunction()

# beast_add_biz_protos(<plugin_name> <scheme_dir> [GLOB] [PROTOS <files...>])
function(beast_add_biz_protos PLUGIN_NAME SCHEME_DIR)
    set(options GLOB)
    set(multi_value PROTOS)
    cmake_parse_arguments(P "${options}" "" "${multi_value}" ${ARGN})

    if(P_GLOB)
        _beast_collect_proto_names(_protos "${SCHEME_DIR}" PROTO_GLOB)
    else()
        _beast_collect_proto_names(_protos "${SCHEME_DIR}" PROTOS ${P_PROTOS})
    endif()

    if(NOT _protos)
        message(WARNING "beast_add_biz_protos(${PLUGIN_NAME}): no .proto found in ${SCHEME_DIR}")
        return()
    endif()

    beast_add_proto_library(plugin_${PLUGIN_NAME}_biz
        PROTO_DIR "${SCHEME_DIR}"
        OUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/biz"
        PROTOS ${_protos}
        COMMENT "Generating ${PLUGIN_NAME} biz scheme protobuf sources"
        DEPS protobuf::libprotobuf)
endfunction()

# beast_add_plugin(<name>
#   [GLOB <patterns...>] [SOURCES <files...>]
#   [PROTO_DIR <dir>] [PROTOS <files...>] [PROTO_GLOB]
#   [BIZ_SCHEME <dir>] [BIZ_GLOB | BIZ_PROTOS <files...>]
# )
function(beast_add_plugin PLUGIN_NAME)
    set(options BIZ_GLOB PROTO_GLOB)
    set(one_value PROTO_DIR BIZ_SCHEME)
    set(multi_value SOURCES GLOB PROTOS BIZ_PROTOS)
    cmake_parse_arguments(P "${options}" "${one_value}" "${multi_value}" ${ARGN})

    _beast_collect_sources(_sources SOURCES ${P_SOURCES} GLOB ${P_GLOB})
    if(NOT _sources)
        message(FATAL_ERROR "beast_add_plugin(${PLUGIN_NAME}): SOURCES or GLOB required")
    endif()

    if(P_BIZ_SCHEME)
        if(P_BIZ_GLOB)
            beast_add_biz_protos(${PLUGIN_NAME} "${P_BIZ_SCHEME}" GLOB)
        elseif(P_BIZ_PROTOS)
            beast_add_biz_protos(${PLUGIN_NAME} "${P_BIZ_SCHEME}" PROTOS ${P_BIZ_PROTOS})
        else()
            message(FATAL_ERROR "beast_add_plugin(${PLUGIN_NAME}): BIZ_SCHEME requires BIZ_GLOB or BIZ_PROTOS")
        endif()
    endif()

    set(_target "beast_plugin_${PLUGIN_NAME}")
    set(_alias "beast::plugin_${PLUGIN_NAME}")
    set(_proto_target "")

    if(P_PROTOS OR P_PROTO_GLOB)
        set(_proto_dir "${CMAKE_CURRENT_SOURCE_DIR}/proto")
        if(P_PROTO_DIR)
            set(_proto_dir "${P_PROTO_DIR}")
        endif()

        if(P_PROTO_GLOB)
            _beast_collect_proto_names(_protos "${_proto_dir}" PROTO_GLOB)
        else()
            _beast_collect_proto_names(_protos "${_proto_dir}" PROTOS ${P_PROTOS})
        endif()
        if(NOT _protos)
            message(FATAL_ERROR "beast_add_plugin(${PLUGIN_NAME}): no communication .proto found in ${_proto_dir}")
        endif()

        beast_add_proto_library(plugin_${PLUGIN_NAME}_proto
            PROTO_DIR "${_proto_dir}"
            PROTOS ${_protos}
            COMMENT "Generating ${PLUGIN_NAME} protobuf sources"
            DEPS protobuf::libprotobuf)
        set(_proto_target "beast_plugin_${PLUGIN_NAME}_proto")
    endif()

    add_library(${_target} SHARED ${_sources})
    add_library(${_alias} ALIAS ${_target})
    set_property(GLOBAL APPEND PROPERTY BEAST_PLUGIN_TARGETS "${_target}")

    target_include_directories(${_target} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(${_target} PRIVATE beast::engine)
    target_compile_features(${_target} PRIVATE cxx_std_20)

    if(_proto_target)
        target_link_libraries(${_target} PRIVATE ${_proto_target})
    endif()
    if(P_BIZ_SCHEME)
        target_link_libraries(${_target} PRIVATE beast_plugin_${PLUGIN_NAME}_biz)
    endif()

    set_target_properties(${_target} PROPERTIES
        PREFIX ""
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins"
    )
endfunction()
