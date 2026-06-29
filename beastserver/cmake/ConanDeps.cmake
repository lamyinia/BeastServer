function(beast_resolve_conan_protobuf_folder out_var)
    foreach(_cfg RELEASE RELWITHDEBINFO DEBUG MINSIZEREL)
        if(DEFINED protobuf_PACKAGE_FOLDER_${_cfg})
            set(${out_var} "${protobuf_PACKAGE_FOLDER_${_cfg}}" PARENT_SCOPE)
            return()
        endif()
    endforeach()
    set(${out_var} "" PARENT_SCOPE)
endfunction()

function(beast_configure_conan_protoc)
    if(NOT TARGET protobuf::protoc)
        return()
    endif()

    beast_resolve_conan_protobuf_folder(_beast_protobuf_pkg)
    if(NOT _beast_protobuf_pkg)
        return()
    endif()

    find_program(_beast_conan_protoc
        NAMES protoc
        PATHS "${_beast_protobuf_pkg}/bin"
        NO_DEFAULT_PATH
    )
    if(_beast_conan_protoc)
        set_property(TARGET protobuf::protoc PROPERTY IMPORTED_LOCATION "${_beast_conan_protoc}")
        set(Protobuf_PROTOC_EXECUTABLE "${_beast_conan_protoc}" CACHE FILEPATH "Conan protoc" FORCE)
        message(STATUS "Using Conan protoc: ${_beast_conan_protoc}")
    endif()
endfunction()

function(beast_find_conan_grpc)
    # Conan (CMakeDeps) 把 gRPCConfig.cmake 与 conan_toolchain.cmake 生成在同一目录。
    # 直接用 ${CMAKE_BINARY_DIR} 在 IDE 场景下不可靠：CLion 的 generation dir 可能
    # 是相对项目根而非源码根，导致 CMAKE_BINARY_DIR 与 Conan 安装目录不一致。
    # CMAKE_TOOLCHAIN_FILE 由 IDE/命令行显式传入绝对路径，取其目录即 Conan 安装目录。
    if(DEFINED CMAKE_TOOLCHAIN_FILE AND EXISTS "${CMAKE_TOOLCHAIN_FILE}")
        get_filename_component(_beast_conan_dir "${CMAKE_TOOLCHAIN_FILE}" DIRECTORY)
    else()
        set(_beast_conan_dir "${CMAKE_BINARY_DIR}")
    endif()
    set(gRPC_DIR "${_beast_conan_dir}" CACHE PATH "Conan gRPC cmake config" FORCE)
    find_package(gRPC CONFIG REQUIRED NO_DEFAULT_PATH)
endfunction()
