# 平台插件扫描入口。BeastPlugin.cmake 已由顶层 CMakeLists.txt include，
# beast_add_plugin / beast_add_biz_protos 函数全局可用。
# 扫描 plugins/<name>/CMakeLists.txt，新增平台插件只需加子目录，不必改本文件。
# 玩法插件请放在顶层 gameplays/ 目录。
file(GLOB _beast_plugin_entries RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/*")
list(SORT _beast_plugin_entries)
foreach(_entry IN LISTS _beast_plugin_entries)
    set(_entry_dir "${CMAKE_CURRENT_SOURCE_DIR}/${_entry}")
    if(IS_DIRECTORY "${_entry_dir}" AND EXISTS "${_entry_dir}/CMakeLists.txt")
        add_subdirectory("${_entry}")
    endif()
endforeach()
