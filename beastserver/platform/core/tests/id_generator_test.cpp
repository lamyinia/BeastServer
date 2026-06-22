#include "beast/platform/core/id_generator.hpp"

#include <gtest/gtest.h>

#include <set>

namespace beast::platform::core {
namespace {

TEST(IdGeneratorTest, GeneratesUniqueIncreasingIds) {
    auto& generator = IdGenerator::instance(1);

    std::set<std::uint64_t> ids;
    std::uint64_t previous = 0;
    for (int i = 0; i < 1000; ++i) {
        const std::uint64_t id = generator.next_id();
        EXPECT_GT(id, previous);
        previous = id;
        EXPECT_TRUE(ids.insert(id).second);
    }
}

TEST(IdGeneratorTest, RejectsInvalidMachineId) {
    auto& generator = IdGenerator::instance(1);
    EXPECT_THROW(generator.set_machine_id(2048), std::invalid_argument);
}

} // namespace
} // namespace beast::platform::core
