#pragma once

#include <iostream>
#include <sstream>
#include <string>

inline int g_tests_failed = 0;

#define EXPECT_TRUE(expr)                                                                          \
    do {                                                                                           \
        if (!(expr)) {                                                                             \
            ++g_tests_failed;                                                                      \
            std::cerr << "EXPECT_TRUE failed: " #expr << " @ " << __FILE__ << ":" << __LINE__ << '\n'; \
        }                                                                                          \
    } while (0)

#define EXPECT_EQ(a, b)                                                                            \
    do {                                                                                           \
        const auto _a = (a);                                                                       \
        const auto _b = (b);                                                                       \
        if (!(_a == _b)) {                                                                         \
            ++g_tests_failed;                                                                      \
            std::cerr << "EXPECT_EQ failed: " #a " != " #b " @ " << __FILE__ << ":" << __LINE__   \
                      << '\n';                                                                     \
        }                                                                                          \
    } while (0)

inline int run_test(const char* name, void (*fn)()) {
    std::cout << "Running " << name << "...\n";
    fn();
    return g_tests_failed;
}
