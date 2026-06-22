#include "beast/platform/net/channel/codec/length_field_decoder.hpp"
#include "beast/platform/net/channel/codec/length_field_encoder.hpp"
#include "beast/platform/net/channel/channel_pipeline.hpp"
#include "beast/platform/net/channel/i_channel.hpp"

#include <gtest/gtest.h>

#include <functional>
#include <memory>
#include <vector>

namespace {

using namespace beast::platform::net::channel;

class FakeChannel final : public IChannel {
public:
    FakeChannel()
        : pipeline_(*this) {}

    [[nodiscard]] ChannelType type() const noexcept override { return ChannelType::Tcp; }
    [[nodiscard]] std::string id() const override { return "codec-test"; }
    [[nodiscard]] bool is_active() const override { return true; }

    void add_inbound(std::shared_ptr<ChannelInboundHandler> handler) override {
        pipeline_.add_inbound(std::move(handler));
    }
    void add_outbound(std::shared_ptr<ChannelOutboundHandler> handler) override {
        pipeline_.add_outbound(std::move(handler));
    }
    void add_duplex(std::shared_ptr<ChannelDuplexHandler> handler) override {
        pipeline_.add_duplex(std::move(handler));
    }
    ChannelPipeline& pipeline() override { return pipeline_; }

    void send(Bytes&& data) override { pipeline_.fire_write(std::move(data)); }
    void flush() override { pipeline_.fire_flush(); }
    void close() override {}
    void start_read() override {}

    void transport_write(Bytes&& data) override { written_.push_back(std::move(data)); }
    void transport_flush() override {}
    void transport_close() override {}

    void set_on_error(OnError) override {}
    void set_on_inactive(OnInactive) override {}
    void dispatch(std::function<void()> fn) override {
        if (fn) {
            fn();
        }
    }

    std::vector<Bytes> written_;

private:
    ChannelPipeline pipeline_;
};

class CaptureInboundHandler final : public ChannelInboundHandler {
public:
    void channel_read(ChannelHandlerContext& ctx, InboundMessage&& msg) override {
        (void)ctx;
        if (std::holds_alternative<Bytes>(msg)) {
            frames_.push_back(std::get<Bytes>(msg));
        }
    }

    std::vector<Bytes> frames_;
};

Bytes make_framed_payload(const std::vector<std::uint8_t>& payload) {
    Bytes framed;
    const auto len = static_cast<std::uint32_t>(payload.size());
    framed.push_back(static_cast<std::uint8_t>((len >> 24) & 0xFF));
    framed.push_back(static_cast<std::uint8_t>((len >> 16) & 0xFF));
    framed.push_back(static_cast<std::uint8_t>((len >> 8) & 0xFF));
    framed.push_back(static_cast<std::uint8_t>(len & 0xFF));
    framed.insert(framed.end(), payload.begin(), payload.end());
    return framed;
}

} // namespace

TEST(LengthFieldCodecTest, EncoderAddsBigEndianLengthPrefix) {
    auto channel = std::make_shared<FakeChannel>();
    channel->add_outbound(std::make_shared<LengthFieldEncoder>());

    Bytes payload{0x01, 0x02, 0x03};
    channel->pipeline().fire_write(Bytes(payload));
    channel->pipeline().fire_flush();

    ASSERT_EQ(channel->written_.size(), 1U);
    ASSERT_EQ(channel->written_[0].size(), 7U);
    EXPECT_EQ(channel->written_[0][0], 0x00);
    EXPECT_EQ(channel->written_[0][1], 0x00);
    EXPECT_EQ(channel->written_[0][2], 0x00);
    EXPECT_EQ(channel->written_[0][3], 0x03);
    EXPECT_EQ(channel->written_[0][4], 0x01);
    EXPECT_EQ(channel->written_[0][5], 0x02);
    EXPECT_EQ(channel->written_[0][6], 0x03);
}

TEST(LengthFieldCodecTest, DecoderHandlesStickyPackets) {
    auto channel = std::make_shared<FakeChannel>();
    auto capture = std::make_shared<CaptureInboundHandler>();
    channel->add_inbound(std::make_shared<LengthFieldDecoder>());
    channel->add_inbound(capture);

    Bytes sticky = make_framed_payload({0xAA});
    const auto second = make_framed_payload({0xBB, 0xCC});
    sticky.insert(sticky.end(), second.begin(), second.end());

    channel->pipeline().fire_channel_read(Bytes(std::move(sticky)));

    ASSERT_EQ(capture->frames_.size(), 2U);
    ASSERT_EQ(capture->frames_[0].size(), 1U);
    EXPECT_EQ(capture->frames_[0][0], 0xAA);
    ASSERT_EQ(capture->frames_[1].size(), 2U);
    EXPECT_EQ(capture->frames_[1][0], 0xBB);
    EXPECT_EQ(capture->frames_[1][1], 0xCC);
}

TEST(LengthFieldCodecTest, DecoderBuffersPartialFrame) {
    auto channel = std::make_shared<FakeChannel>();
    auto capture = std::make_shared<CaptureInboundHandler>();
    channel->add_inbound(std::make_shared<LengthFieldDecoder>());
    channel->add_inbound(capture);

    Bytes partial{0x00, 0x00, 0x00, 0x02, 0x11};
    channel->pipeline().fire_channel_read(Bytes(partial));
    EXPECT_TRUE(capture->frames_.empty());

    Bytes rest{0x22};
    channel->pipeline().fire_channel_read(Bytes(std::move(rest)));

    ASSERT_EQ(capture->frames_.size(), 1U);
    ASSERT_EQ(capture->frames_[0].size(), 2U);
    EXPECT_EQ(capture->frames_[0][0], 0x11);
    EXPECT_EQ(capture->frames_[0][1], 0x22);
}
