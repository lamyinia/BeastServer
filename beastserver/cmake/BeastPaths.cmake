# 仓库根目录（beastserver/ 的上一级），供 bizconfig 等兄弟目录引用。
get_filename_component(BEAST_REPO_ROOT "${CMAKE_SOURCE_DIR}/.." ABSOLUTE)

set(BEAST_BIZCONFIG_DIR "${BEAST_REPO_ROOT}/bizconfig" CACHE PATH "Bizconfig root (scheme + protocol + static-xlsx)")
set(BEAST_BIZ_SCHEME_DIR "${BEAST_BIZCONFIG_DIR}/scheme")
set(BEAST_BIZ_PROTOCOL_DIR "${BEAST_BIZCONFIG_DIR}/protocol")
set(BEAST_BIZ_PROTOCOL_PLATFORM_DIR "${BEAST_BIZ_PROTOCOL_DIR}/platform")
set(BEAST_BIZ_PROTOCOL_GAME_DIR "${BEAST_BIZ_PROTOCOL_DIR}/game")
