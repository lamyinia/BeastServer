#include "beast/platform/net/outbound/route_reliability_registry.hpp"
#include "beast/platform/net/transport/unreliable_frame.hpp"

#include <gtest/gtest.h>

#include <vector>

namespace {
namespace transport = beast::platform::net::transport;
namespace outbound = beast::platform::net::outbound;
} // namespace

// ========== UnreliableFrame encode/decode ==========

TEST(UnreliableFrameTest, EncodeDecodeRoundTrip) {
    transport::UnreliableFrame original{
        .magic = transport::kUnreliableFrameMagic,
        .route_id = 0x1234,
        .seq = 0xDEADBEEFu,
        .payload = {0x01, 0x02, 0x03, 0x04, 0x05},
    };

    const auto encoded = transport::encode_unreliable_frame(original);
    ASSERT_EQ(encoded.size(), transport::kUnreliableFrameHeaderSize + original.payload.size());

    const auto decoded = transport::decode_unreliable_frame(encoded);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->magic, original.magic);
    EXPECT_EQ(decoded->route_id, original.route_id);
    EXPECT_EQ(decoded->seq, original.seq);
    EXPECT_EQ(decoded->payload, original.payload);
}

TEST(UnreliableFrameTest, EncodeBigEndianFields) {
    transport::UnreliableFrame frame{
        .magic = 0xBEEF,
        .route_id = 0xABCD,
        .seq = 0x12345678u,
        .payload = {},
    };

    const auto encoded = transport::encode_unreliable_frame(frame);
    ASSERT_GE(encoded.size(), transport::kUnreliableFrameHeaderSize);

    // magic BE: 0xBE 0xEF
    EXPECT_EQ(encoded[0], 0xBE);
    EXPECT_EQ(encoded[1], 0xEF);
    // route_id BE: 0xAB 0xCD
    EXPECT_EQ(encoded[2], 0xAB);
    EXPECT_EQ(encoded[3], 0xCD);
    // seq BE: 0x12 0x34 0x56 0x78
    EXPECT_EQ(encoded[4], 0x12);
    EXPECT_EQ(encoded[5], 0x34);
    EXPECT_EQ(encoded[6], 0x56);
    EXPECT_EQ(encoded[7], 0x78);
}

TEST(UnreliableFrameTest, EncodeEmptyPayload) {
    transport::UnreliableFrame frame{
        .magic = transport::kUnreliableFrameMagic,
        .route_id = 0,
        .seq = 0,
        .payload = {},
    };

    const auto encoded = transport::encode_unreliable_frame(frame);
    EXPECT_EQ(encoded.size(), transport::kUnreliableFrameHeaderSize);

    const auto decoded = transport::decode_unreliable_frame(encoded);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_TRUE(decoded->payload.empty());
}

TEST(UnreliableFrameTest, DecodeTooShort) {
    std::vector<std::uint8_t> short_buf(7, 0xFF); // 7 bytes < 8 header
    const auto decoded = transport::decode_unreliable_frame(short_buf);
    EXPECT_FALSE(decoded.has_value());
}

TEST(UnreliableFrameTest, DecodeEmptyBuffer) {
    std::vector<std::uint8_t> empty_buf;
    const auto decoded = transport::decode_unreliable_frame(empty_buf);
    EXPECT_FALSE(decoded.has_value());
}

TEST(UnreliableFrameTest, DecodeRawPointerVariant) {
    transport::UnreliableFrame frame{
        .magic = transport::kUnreliableFrameMagic,
        .route_id = 0x4242,
        .seq = 99u,
        .payload = {0xAA, 0xBB},
    };
    const auto encoded = transport::encode_unreliable_frame(frame);
    const auto decoded = transport::decode_unreliable_frame(encoded.data(), encoded.size());
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->route_id, 0x4242u);
    EXPECT_EQ(decoded->seq, 99u);
    EXPECT_EQ(decoded->payload, (std::vector<std::uint8_t>{0xAA, 0xBB}));
}

// ========== is_unreliable_frame magic detection ==========

TEST(UnreliableFrameTest, IsUnreliableFrameMatchesMagic) {
    std::vector<std::uint8_t> buf{0xBE, 0xEF, 0x00, 0x01};
    EXPECT_TRUE(transport::is_unreliable_frame(buf));
}

TEST(UnreliableFrameTest, IsUnreliableFrameRejectsKcpConv) {
    // KCP default conv=1 → wire starts with 0x00 0x00
    std::vector<std::uint8_t> buf{0x00, 0x00, 0x01, 0x00};
    EXPECT_FALSE(transport::is_unreliable_frame(buf));
}

TEST(UnreliableFrameTest, IsUnreliableFrameRejectsShortBuffer) {
    std::vector<std::uint8_t> buf{0xBE};
    EXPECT_FALSE(transport::is_unreliable_frame(buf));
}

TEST(UnreliableFrameTest, IsUnreliableFrameCustomMagic) {
    const std::uint16_t custom_magic = 0xCAFE;
    std::vector<std::uint8_t> buf{0xCA, 0xFE, 0x00, 0x01};
    EXPECT_TRUE(transport::is_unreliable_frame(buf, custom_magic));
    EXPECT_FALSE(transport::is_unreliable_frame(buf)); // default magic doesn't match
}

TEST(UnreliableFrameTest, IsUnreliableFrameRawPointer) {
    const std::uint8_t data[] = {0xBE, 0xEF};
    EXPECT_TRUE(transport::is_unreliable_frame(data, 2));
    EXPECT_FALSE(transport::is_unreliable_frame(data, 1));
}

// ========== OutboundRouteRegistry hash + reverse lookup ==========

TEST(OutboundRouteRegistryTest, HashIsDeterministic) {
    const auto h1 = outbound::OutboundRouteRegistry::route_id_hash("pixelmoba.transform");
    const auto h2 = outbound::OutboundRouteRegistry::route_id_hash("pixelmoba.transform");
    EXPECT_EQ(h1, h2);
}

TEST(OutboundRouteRegistryTest, HashDiffersForDifferentRoutes) {
    const auto h1 = outbound::OutboundRouteRegistry::route_id_hash("pixelmoba.transform");
    const auto h2 = outbound::OutboundRouteRegistry::route_id_hash("pixelmoba.projectile");
    EXPECT_NE(h1, h2);
}

TEST(OutboundRouteRegistryTest, DeclareAndReverseLookup) {
    outbound::OutboundRouteRegistry registry;
    registry.declare("test.route", outbound::RouteReliability::Unreliable);

    EXPECT_TRUE(registry.is_unreliable("test.route"));
    EXPECT_FALSE(registry.is_unreliable("unknown.route"));

    const auto hash = outbound::OutboundRouteRegistry::route_id_hash("test.route");
    const auto name = registry.route_name_for(hash);
    ASSERT_TRUE(name.has_value());
    EXPECT_EQ(*name, "test.route");
}

TEST(OutboundRouteRegistryTest, DefaultReliabilityIsReliable) {
    outbound::OutboundRouteRegistry registry;
    EXPECT_EQ(registry.reliability_of("undeclared.route"), outbound::RouteReliability::Reliable);
    EXPECT_FALSE(registry.is_unreliable("undeclared.route"));
}

TEST(OutboundRouteRegistryTest, ReverseLookupUnknownHash) {
    outbound::OutboundRouteRegistry registry;
    const auto name = registry.route_name_for(0xFFFF);
    EXPECT_FALSE(name.has_value());
}

TEST(OutboundRouteRegistryTest, DeclareReliableDoesNotMarkUnreliable) {
    outbound::OutboundRouteRegistry registry;
    registry.declare("reliable.route", outbound::RouteReliability::Reliable);
    EXPECT_FALSE(registry.is_unreliable("reliable.route"));

    // But reverse lookup should still work (hash filled either way)
    const auto hash = outbound::OutboundRouteRegistry::route_id_hash("reliable.route");
    const auto name = registry.route_name_for(hash);
    ASSERT_TRUE(name.has_value());
    EXPECT_EQ(*name, "reliable.route");
}
