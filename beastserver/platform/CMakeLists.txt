include(${CMAKE_SOURCE_DIR}/cmake/BeastTargets.cmake)

add_subdirectory(core)
add_subdirectory(bizutil)
add_subdirectory(ai)
add_subdirectory(net)
add_subdirectory(engine)
add_subdirectory(rpc)
add_subdirectory(discovery)
# 平台插件（如 ai_integration）须在 server 之前定义，因 server 链接 ai_integration
add_subdirectory(${CMAKE_SOURCE_DIR}/plugins ${CMAKE_BINARY_DIR}/plugins)
add_subdirectory(server)