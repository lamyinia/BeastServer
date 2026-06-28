#include "../tests/test_main.hpp"

#include "beast/client/event_queue.hpp"

using namespace beast::client;

static void test_event_queue_push_pop() {
    EventQueue<int> queue;
    queue.push(1);
    queue.push(2);

    const std::optional<int> first = queue.try_pop();
    EXPECT_TRUE(first.has_value());
    EXPECT_EQ(*first, 1);

    const std::optional<int> second = queue.try_pop();
    EXPECT_TRUE(second.has_value());
    EXPECT_EQ(*second, 2);

    const std::optional<int> empty = queue.try_pop();
    EXPECT_TRUE(!empty.has_value());
}

static void test_event_queue_clear() {
    EventQueue<int> queue;
    queue.push(42);
    queue.clear();
    EXPECT_TRUE(!queue.try_pop().has_value());
}

int main() {
    run_test("event_queue_push_pop", test_event_queue_push_pop);
    run_test("event_queue_clear", test_event_queue_clear);

    if (g_tests_failed == 0) {
        std::cout << "All io thread tests passed\n";
        return 0;
    }
    std::cout << g_tests_failed << " test(s) failed\n";
    return 1;
}
