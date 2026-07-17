find_package(spdlog REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(GTest REQUIRED)

beast_add_library(core
    GLOB src/*.cpp
    DEPS spdlog::spdlog nlohmann_json::nlohmann_json)

enable_testing()

add_executable(beast_core_id_generator_test tests/id_generator_test.cpp)
target_link_libraries(beast_core_id_generator_test PRIVATE beast_core GTest::gtest_main)
add_test(NAME beast_core_id_generator_test COMMAND beast_core_id_generator_test)

add_executable(beast_core_config_test tests/config_test.cpp)
target_compile_definitions(beast_core_config_test PRIVATE BEASTSERVER_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_link_libraries(beast_core_config_test PRIVATE beast_core GTest::gtest_main)
add_test(NAME beast_core_config_test COMMAND beast_core_config_test)
