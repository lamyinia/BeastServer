beast_add_plugin(pixelmoba
    GLOB plugin.cpp engine/*.cpp
    PROTO_DIR "${BEAST_BIZ_PROTOCOL_DIR}/game/moba/pixel_moba"
    PROTO_GLOB
    BIZ_SCHEME "${BEAST_BIZ_SCHEME_DIR}/moba/pixel_moba"
    BIZ_GLOB)
