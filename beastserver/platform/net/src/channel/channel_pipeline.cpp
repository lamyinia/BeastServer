#include "beast/platform/net/channel/channel_pipeline.hpp"
#include "beast/platform/net/channel/i_channel.hpp"
#include "beast/platform/net/channel/message.hpp"

namespace beast::platform::net::channel {

DefaultChannelHandlerContext::DefaultChannelHandlerContext(
    IChannel& channel,
    ChannelPipeline& pipeline,
    const std::size_t index,
    std::shared_ptr<ChannelInboundHandler> inbound,
    std::shared_ptr<ChannelOutboundHandler> outbound)
    : channel_(channel)
    , pipeline_(pipeline)
    , index_(index)
    , inbound_(std::move(inbound))
    , outbound_(std::move(outbound)) {}

void DefaultChannelHandlerContext::set_authorized(const std::string& player_id) {
    pipeline_.authorized_ = true;
    pipeline_.player_id_ = player_id;
}

bool DefaultChannelHandlerContext::is_authorized() const noexcept {
    return pipeline_.authorized_;
}

const std::string& DefaultChannelHandlerContext::player_id() const noexcept {
    return pipeline_.player_id_;
}

const std::string& DefaultChannelHandlerContext::instance_id() const noexcept {
    return pipeline_.pipeline_instance_id();
}

void DefaultChannelHandlerContext::set_instance_id(const std::string& instance_id) {
    pipeline_.set_pipeline_instance_id(instance_id);
}

void DefaultChannelHandlerContext::clear_instance_id() {
    pipeline_.clear_pipeline_instance_binding();
}

bool DefaultChannelHandlerContext::has_instance_id() const noexcept {
    return pipeline_.pipeline_has_instance_id();
}

void* DefaultChannelHandlerContext::instance_dispatch_handle() const noexcept {
    return pipeline_.pipeline_instance_dispatch_handle();
}

void DefaultChannelHandlerContext::set_instance_dispatch_handle(void* handle) {
    pipeline_.instance_dispatch_ = handle;
}

void DefaultChannelHandlerContext::clear_instance_dispatch_handle() {
    pipeline_.instance_dispatch_ = nullptr;
}

bool DefaultChannelHandlerContext::has_instance_dispatch_handle() const noexcept {
    return pipeline_.pipeline_has_instance_dispatch_handle();
}

void DefaultChannelHandlerContext::fire_channel_active() {
    pipeline_.fire_channel_active_from(index_ + 1);
}

void DefaultChannelHandlerContext::fire_channel_read(InboundMessage&& msg) {
    pipeline_.fire_channel_read_from(index_ + 1, std::move(msg));
}

void DefaultChannelHandlerContext::fire_channel_inactive() {
    pipeline_.fire_channel_inactive_from(index_ + 1);
}

void DefaultChannelHandlerContext::fire_exception_caught(const std::error_code& ec) {
    pipeline_.fire_exception_caught_from(index_ + 1, ec);
}

void DefaultChannelHandlerContext::fire_write(OutboundMessage&& msg) {
    pipeline_.fire_write_from(index_ - 1, std::move(msg));
}

void DefaultChannelHandlerContext::fire_flush() {
    pipeline_.fire_flush_from(index_ - 1);
}

void DefaultChannelHandlerContext::fire_close() {
    pipeline_.fire_close_from(index_ - 1);
}

ChannelPipeline::ChannelPipeline(IChannel& channel)
    : channel_(channel) {}

void ChannelPipeline::add_inbound(std::shared_ptr<ChannelInboundHandler> handler) {
    auto ctx = std::make_shared<DefaultChannelHandlerContext>(
        channel_, *this, contexts_.size(), std::move(handler), nullptr);
    contexts_.push_back(std::move(ctx));
}

void ChannelPipeline::add_outbound(std::shared_ptr<ChannelOutboundHandler> handler) {
    auto ctx = std::make_shared<DefaultChannelHandlerContext>(
        channel_, *this, contexts_.size(), nullptr, std::move(handler));
    contexts_.push_back(std::move(ctx));
}

void ChannelPipeline::add_duplex(std::shared_ptr<ChannelDuplexHandler> handler) {
    auto inbound_ptr = std::static_pointer_cast<ChannelInboundHandler>(handler);
    auto outbound_ptr = std::static_pointer_cast<ChannelOutboundHandler>(handler);
    auto ctx = std::make_shared<DefaultChannelHandlerContext>(
        channel_, *this, contexts_.size(), inbound_ptr, outbound_ptr);
    contexts_.push_back(std::move(ctx));
}

void ChannelPipeline::clear() {
    contexts_.clear();
}

void ChannelPipeline::fire_channel_active() {
    fire_channel_active_from(0);
}

void ChannelPipeline::fire_channel_read(InboundMessage&& msg) {
    fire_channel_read_from(0, std::move(msg));
}

void ChannelPipeline::fire_channel_inactive() {
    fire_channel_inactive_from(0);
}

void ChannelPipeline::fire_exception_caught(const std::error_code& ec) {
    fire_exception_caught_from(0, ec);
}

void ChannelPipeline::fire_write(OutboundMessage&& msg) {
    if (contexts_.empty()) {
        if (std::holds_alternative<Bytes>(msg)) {
            channel_.transport_write(std::move(std::get<Bytes>(msg)));
        }
        return;
    }
    fire_write_from(contexts_.size() - 1, std::move(msg));
}

void ChannelPipeline::fire_flush() {
    if (contexts_.empty()) {
        channel_.transport_flush();
        return;
    }
    fire_flush_from(contexts_.size() - 1);
}

void ChannelPipeline::fire_close() {
    if (contexts_.empty()) {
        channel_.transport_close();
        return;
    }
    fire_close_from(contexts_.size() - 1);
}

void ChannelPipeline::fire_channel_active_from(const std::size_t index) {
    for (std::size_t i = index; i < contexts_.size(); ++i) {
        if (auto* handler = contexts_[i]->inbound_handler()) {
            handler->channel_active(*contexts_[i]);
        }
    }
}

void ChannelPipeline::fire_channel_read_from(const std::size_t index, InboundMessage&& msg) {
    for (std::size_t i = index; i < contexts_.size(); ++i) {
        if (auto* handler = contexts_[i]->inbound_handler()) {
            handler->channel_read(*contexts_[i], std::move(msg));
            return;
        }
    }
}

void ChannelPipeline::fire_channel_inactive_from(const std::size_t index) {
    for (std::size_t i = index; i < contexts_.size(); ++i) {
        if (auto* handler = contexts_[i]->inbound_handler()) {
            handler->channel_inactive(*contexts_[i]);
        }
    }
}

void ChannelPipeline::fire_exception_caught_from(const std::size_t index, const std::error_code& ec) {
    for (std::size_t i = index; i < contexts_.size(); ++i) {
        if (auto* handler = contexts_[i]->inbound_handler()) {
            handler->exception_caught(*contexts_[i], ec);
        }
    }
}

void ChannelPipeline::fire_write_from(const std::size_t index, OutboundMessage&& msg) {
    for (int i = static_cast<int>(index); i >= 0; --i) {
        if (auto* handler = contexts_[i]->outbound_handler()) {
            handler->write(*contexts_[i], std::move(msg));
            return;
        }
    }
    if (std::holds_alternative<Bytes>(msg)) {
        channel_.transport_write(std::move(std::get<Bytes>(msg)));
    }
}

void ChannelPipeline::fire_flush_from(const std::size_t index) {
    for (int i = static_cast<int>(index); i >= 0; --i) {
        if (auto* handler = contexts_[i]->outbound_handler()) {
            handler->flush(*contexts_[i]);
            return;
        }
    }
    channel_.transport_flush();
}

void ChannelPipeline::fire_close_from(const std::size_t index) {
    for (int i = static_cast<int>(index); i >= 0; --i) {
        if (auto* handler = contexts_[i]->outbound_handler()) {
            handler->close(*contexts_[i]);
        }
    }
    channel_.transport_close();
}

void ChannelPipeline::set_pipeline_instance_id(const std::string& instance_id) {
    instance_id_ = instance_id;
}

void ChannelPipeline::clear_pipeline_instance_id() {
    clear_pipeline_instance_binding();
}

const std::string& ChannelPipeline::pipeline_instance_id() const noexcept {
    return instance_id_;
}

bool ChannelPipeline::pipeline_has_instance_id() const noexcept {
    return !instance_id_.empty();
}

void ChannelPipeline::set_pipeline_instance_binding(
    const std::string& instance_id,
    void* dispatch_handle) {
    instance_id_ = instance_id;
    instance_dispatch_ = dispatch_handle;
}

void ChannelPipeline::clear_pipeline_instance_binding() {
    instance_id_.clear();
    instance_dispatch_ = nullptr;
}

void* ChannelPipeline::pipeline_instance_dispatch_handle() const noexcept {
    return instance_dispatch_;
}

bool ChannelPipeline::pipeline_has_instance_dispatch_handle() const noexcept {
    return instance_dispatch_ != nullptr;
}

void ChannelHandlerContext::send_error_response(
    const std::string& route,
    const std::uint64_t client_seq,
    const std::string& error) {
    auto resp_msg = std::make_shared<Message>();
    resp_msg->route = route + ".response";
    resp_msg->client_seq = client_seq;

    const std::string error_payload = R"({"error":")" + error + R"("})";
    resp_msg->payload.assign(error_payload.begin(), error_payload.end());

    fire_write(MessagePtr(std::move(resp_msg)));
    fire_flush();
}

void ChannelHandlerContext::send_error_response(const MessagePtr& msg, const std::string& error) {
    send_error_response(msg->route, msg->client_seq, error);
}

} // namespace beast::platform::net::channel
