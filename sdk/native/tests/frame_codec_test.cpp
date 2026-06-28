#include "beast/client/frame_codec.hpp"

#include "../tests/test_main.hpp"

static void test_frame_encode_decode_roundtrip() {
    using namespace beast::client;

    const Bytes body{0x01, 0x02, 0x03};
    const Bytes framed = frame_encode(body);
    EXPECT_EQ(framed.size(), 7U);
    EXPECT_EQ(framed[0], 0x00);
    EXPECT_EQ(framed[1], 0x00);
    EXPECT_EQ(framed[2], 0x00);
    EXPECT_EQ(framed[3], 0x03);

    const FrameDecodeResult decoded = frame_try_decode(framed);
    EXPECT_EQ(decoded.frames.size(), 1U);
    EXPECT_EQ(decoded.frames[0], body);
    EXPECT_TRUE(decoded.remaining.empty());
}

static void test_frame_sticky_packets() {
    using namespace beast::client;

    Bytes sticky = frame_encode(Bytes{0xAA});
    const Bytes second = frame_encode(Bytes{0xBB, 0xCC});
    sticky.insert(sticky.end(), second.begin(), second.end());

    const FrameDecodeResult decoded = frame_try_decode(sticky);
    EXPECT_EQ(decoded.frames.size(), 2U);
    EXPECT_EQ(decoded.frames[0].size(), 1U);
    EXPECT_EQ(decoded.frames[0][0], 0xAA);
    EXPECT_EQ(decoded.frames[1].size(), 2U);
}

int main() {
    run_test("frame_encode_decode_roundtrip", test_frame_encode_decode_roundtrip);
    run_test("frame_sticky_packets", test_frame_sticky_packets);

    if (g_tests_failed == 0) {
        std::cout << "All native tests passed\n";
        return 0;
    }
    std::cout << g_tests_failed << " test(s) failed\n";
    return 1;
}
