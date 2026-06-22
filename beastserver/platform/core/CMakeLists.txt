find_package(spdlog REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(GTest REQUIRED)

add_library(beast_core STATIC
    src/log/logger.cpp
    src/time.cpp
    src/id_generator.cpp
    src/config/server_config_json.cpp
    src/config/plugins_config.cpp
)

add_library(beast::core ALIAS beast_core)

target_include_directories(beast_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(beast_core
    PUBLIC
        spdlog::spdlog
        nlohmann_json::nlohmann_json
)

target_compile_features(beast_core PUBLIC cxx_std_20)

enable_testing()

add_executable(beast_core_id_generator_test tests/id_generator_test.cpp)
target_link_libraries(beast_core_id_generator_test PRIVATE beast_core GTest::gtest_main)
add_test(NAME beast_core_id_generator_test COMMAND beast_core_id_generator_test)

add_executable(beast_core_config_test tests/config_test.cpp)
target_compile_definitions(beast_core_config_test PRIVATE BEASTSERVER_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_link_libraries(beast_core_config_test PRIVATE beast_core GTest::gtest_main)
add_test(NAME beast_core_config_test COMMAND beast_core_config_test)
