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
    set(gRPC_DIR "${CMAKE_BINARY_DIR}" CACHE PATH "Conan gRPC cmake config" FORCE)
    find_package(gRPC CONFIG REQUIRED NO_DEFAULT_PATH)
endfunction()
