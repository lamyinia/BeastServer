#include "beast/platform/discovery/etcd_client.hpp"

#include <grpcpp/grpcpp.h>
#include <etcdserverpb/rpc.grpc.pb.h>
#include <mvccpb/kv.pb.h>

#include "beast/platform/core/log/logger.hpp"

#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace beast::platform::discovery {

using KVStub = etcdserverpb::KV::Stub;
using LeaseStub = etcdserverpb::Lease::Stub;
using WatchStub = etcdserverpb::Watch::Stub;

struct EtcdClient::Impl {
    std::shared_ptr<grpc::Channel> channel;
    std::unique_ptr<KVStub> kv_stub;
    std::unique_ptr<LeaseStub> lease_stub;
    std::unique_ptr<WatchStub> watch_stub;
    std::atomic<bool> connected{false};

    std::mutex keepalive_mutex;
    std::unordered_map<std::int64_t, std::unique_ptr<std::thread>> keepalive_threads;
    std::unordered_map<std::int64_t, std::atomic<bool>> keepalive_running;

    std::mutex watch_mutex;
    std::atomic<std::uint64_t> next_watch_id{1};
    std::unordered_map<WatchId, std::unique_ptr<std::thread>> watch_threads;
    std::unordered_map<WatchId, std::atomic<bool>> watch_running;
    std::unordered_map<WatchId, WatchCallback> watch_callbacks;
    // 持有每个 watch 的 ClientContext,cancel 时 TryCancel 强制中断阻塞的 Read
    std::unordered_map<WatchId, std::unique_ptr<grpc::ClientContext>> watch_contexts;
};

EtcdClient::EtcdClient() : impl_(std::make_unique<Impl>()) {}

EtcdClient::~EtcdClient() { disconnect(); }

bool EtcdClient::connect(const std::string &endpoints) {
    if (impl_->connected) {
        return true;
    }

    try {
        std::string target = endpoints;
        if (target.find("http://") == 0) {
            target = target.substr(7);
        } else if (target.find("https://") == 0) {
            target = target.substr(8);
        }

        grpc::ChannelArguments args;
        args.SetMaxReceiveMessageSize(64 * 1024 * 1024);
        impl_->channel = grpc::CreateCustomChannel(target, grpc::InsecureChannelCredentials(), args);

        auto deadline = std::chrono::system_clock::now() + std::chrono::seconds(5);
        if (!impl_->channel->WaitForConnected(deadline)) {
            BEAST_LOG_WARN("[EtcdClient] failed to connect to: {}", target);
            return false;
        }

        impl_->kv_stub = etcdserverpb::KV::NewStub(impl_->channel);
        impl_->lease_stub = etcdserverpb::Lease::NewStub(impl_->channel);
        impl_->watch_stub = etcdserverpb::Watch::NewStub(impl_->channel);

        etcdserverpb::RangeRequest req;
        req.set_key("/__etcd_client_test__");
        req.set_limit(1);
        etcdserverpb::RangeResponse resp;
        grpc::ClientContext ctx;
        ctx.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(3));
        auto status = impl_->kv_stub->Range(&ctx, req, &resp);
        impl_->connected = status.ok() || status.error_code() == grpc::StatusCode::NOT_FOUND;
        if (!impl_->connected) {
            BEAST_LOG_WARN("[EtcdClient] Range verify failed: error_code={} error_msg={}",
                           static_cast<int>(status.error_code()), status.error_message());
        }
        return impl_->connected.load();
    } catch (const std::exception &e) {
        BEAST_LOG_WARN("[EtcdClient] connect exception: {}", e.what());
        return false;
    }
}

bool EtcdClient::is_connected() const noexcept { return impl_->connected; }

void EtcdClient::disconnect() {
    if (!impl_->connected) {
        return;
    }

    cancel_all_watches();

    {
        std::lock_guard lock(impl_->keepalive_mutex);
        for (auto &[lease_id, running] : impl_->keepalive_running) {
            running.store(false);
        }
        for (auto &[lease_id, thread] : impl_->keepalive_threads) {
            if (thread && thread->joinable()) {
                thread->join();
            }
        }
        impl_->keepalive_threads.clear();
        impl_->keepalive_running.clear();
    }

    impl_->kv_stub.reset();
    impl_->lease_stub.reset();
    impl_->watch_stub.reset();
    impl_->channel.reset();
    impl_->connected = false;
}

bool EtcdClient::put(std::string_view key, std::string_view value, std::int64_t lease_id) {
    if (!impl_->connected || !impl_->kv_stub) {
        return false;
    }

    etcdserverpb::PutRequest req;
    req.set_key(std::string(key));
    req.set_value(std::string(value));
    if (lease_id > 0) {
        req.set_lease(lease_id);
    }

    etcdserverpb::PutResponse resp;
    grpc::ClientContext ctx;
    ctx.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(5));

    auto status = impl_->kv_stub->Put(&ctx, req, &resp);
    if (!status.ok()) {
        BEAST_LOG_WARN("[EtcdClient] put failed: {}", status.error_message());
    }
    return status.ok();
}

std::optional<std::string> EtcdClient::get(std::string_view key) {
    if (!impl_->connected || !impl_->kv_stub) {
        return std::nullopt;
    }

    etcdserverpb::RangeRequest req;
    req.set_key(std::string(key));

    etcdserverpb::RangeResponse resp;
    grpc::ClientContext ctx;
    ctx.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(5));

    auto status = impl_->kv_stub->Range(&ctx, req, &resp);
    if (!status.ok()) {
        BEAST_LOG_WARN("[EtcdClient] get failed: {}", status.error_message());
        return std::nullopt;
    }
    if (resp.kvs_size() > 0) {
        return resp.kvs(0).value();
    }
    return std::nullopt;
}

bool EtcdClient::del(std::string_view key) {
    if (!impl_->connected || !impl_->kv_stub) {
        return false;
    }

    etcdserverpb::DeleteRangeRequest req;
    req.set_key(std::string(key));

    etcdserverpb::DeleteRangeResponse resp;
    grpc::ClientContext ctx;
    ctx.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(5));

    auto status = impl_->kv_stub->DeleteRange(&ctx, req, &resp);
    return status.ok();
}

std::vector<std::pair<std::string, std::string>> EtcdClient::get_by_prefix(std::string_view prefix) {
    std::vector<std::pair<std::string, std::string>> result;
    if (!impl_->connected || !impl_->kv_stub) {
        return result;
    }

    etcdserverpb::RangeRequest req;
    req.set_key(std::string(prefix));
    std::string range_end(prefix);
    if (!range_end.empty()) {
        range_end.back() += 1;
    }
    req.set_range_end(range_end);

    etcdserverpb::RangeResponse resp;
    grpc::ClientContext ctx;
    ctx.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(5));

    auto status = impl_->kv_stub->Range(&ctx, req, &resp);
    if (status.ok()) {
        for (const auto &kv : resp.kvs()) {
            result.emplace_back(kv.key(), kv.value());
        }
    }
    return result;
}

std::int64_t EtcdClient::grant_lease(std::int64_t ttl_seconds) {
    if (!impl_->connected || !impl_->lease_stub) {
        return 0;
    }

    etcdserverpb::LeaseGrantRequest req;
    req.set_ttl(ttl_seconds);

    etcdserverpb::LeaseGrantResponse resp;
    grpc::ClientContext ctx;
    ctx.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(5));

    auto status = impl_->lease_stub->LeaseGrant(&ctx, req, &resp);
    if (status.ok() && resp.id() != 0) {
        return resp.id();
    }
    return 0;
}

bool EtcdClient::start_keepalive(std::int64_t ttl_seconds, std::int64_t lease_id) {
    if (!impl_->connected || !impl_->lease_stub || lease_id == 0) {
        return false;
    }

    std::lock_guard lock(impl_->keepalive_mutex);
    if (impl_->keepalive_threads.contains(lease_id)) {
        return true;
    }

    impl_->keepalive_running.emplace(lease_id, false);
    auto &running = impl_->keepalive_running.at(lease_id);
    running.store(true);

    auto lease_stub_ptr = impl_->lease_stub.get();

    impl_->keepalive_threads.emplace(lease_id, std::make_unique<std::thread>(
        [lease_stub_ptr, lease_id, ttl_seconds, &running]() {
            grpc::ClientContext ctx;
            auto stream = lease_stub_ptr->LeaseKeepAlive(&ctx);

            while (running.load()) {
                etcdserverpb::LeaseKeepAliveRequest ka_req;
                ka_req.set_id(lease_id);

                if (!stream->Write(ka_req)) {
                    break;
                }
                etcdserverpb::LeaseKeepAliveResponse resp;
                if (!stream->Read(&resp)) {
                    break;
                }

                auto sleep_duration = std::chrono::seconds(std::max<std::int64_t>(1, ttl_seconds / 3));
                for (int i = 0; i < sleep_duration.count() * 10 && running.load(); ++i) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
            }

            running.store(false);
            stream->WritesDone();
            stream->Finish();
        }));

    return true;
}

bool EtcdClient::is_keepalive_active(std::int64_t lease_id) {
    std::lock_guard lock(impl_->keepalive_mutex);
    const auto running_it = impl_->keepalive_running.find(lease_id);
    if (running_it == impl_->keepalive_running.end() || !running_it->second.load()) {
        return false;
    }
    const auto thread_it = impl_->keepalive_threads.find(lease_id);
    return thread_it != impl_->keepalive_threads.end()
           && thread_it->second != nullptr
           && thread_it->second->joinable();
}

void EtcdClient::stop_keepalive(std::int64_t lease_id) {
    std::lock_guard lock(impl_->keepalive_mutex);
    if (auto it = impl_->keepalive_running.find(lease_id); it != impl_->keepalive_running.end()) {
        it->second.store(false);
    }
    if (auto it = impl_->keepalive_threads.find(lease_id); it != impl_->keepalive_threads.end()) {
        if (it->second && it->second->joinable()) {
            it->second->join();
        }
        impl_->keepalive_threads.erase(it);
    }
    impl_->keepalive_running.erase(lease_id);
}

bool EtcdClient::revoke_lease(std::int64_t lease_id) {
    if (!impl_->connected || !impl_->lease_stub || lease_id == 0) {
        return false;
    }
    stop_keepalive(lease_id);

    etcdserverpb::LeaseRevokeRequest req;
    req.set_id(lease_id);

    etcdserverpb::LeaseRevokeResponse resp;
    grpc::ClientContext ctx;
    ctx.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(5));

    auto status = impl_->lease_stub->LeaseRevoke(&ctx, req, &resp);
    return status.ok();
}

EtcdClient::WatchId EtcdClient::watch(std::string_view prefix, WatchCallback callback) {
    if (!impl_->connected || !impl_->watch_stub || !callback) {
        return 0;
    }

    std::lock_guard lock(impl_->watch_mutex);
    WatchId watch_id = impl_->next_watch_id++;

    impl_->watch_running.emplace(watch_id, true);
    auto &running = impl_->watch_running.at(watch_id);
    impl_->watch_callbacks.emplace(watch_id, std::move(callback));

    auto watch_stub_ptr = impl_->watch_stub.get();
    auto &watch_callbacks = impl_->watch_callbacks;
    auto &watch_contexts = impl_->watch_contexts;

    std::string prefix_str(prefix);
    std::string range_end(prefix);
    if (!range_end.empty()) {
        range_end.back() += 1;
    }

    // 持有 ClientContext 在 Impl 中,cancel 时可 TryCancel 中断阻塞 Read
    auto ctx_ptr = std::make_unique<grpc::ClientContext>();
    auto *ctx_raw = ctx_ptr.get();
    impl_->watch_contexts.emplace(watch_id, std::move(ctx_ptr));

    impl_->watch_threads.emplace(watch_id, std::make_unique<std::thread>(
        [watch_stub_ptr, watch_id, prefix_str, range_end, ctx_raw, &running, &watch_callbacks, &watch_contexts]() {
            auto stream = watch_stub_ptr->Watch(ctx_raw);

            etcdserverpb::WatchRequest req;
            auto *create = req.mutable_create_request();
            create->set_key(prefix_str);
            create->set_range_end(range_end);

            if (!stream->Write(req)) {
                running.store(false);
                stream->WritesDone();
                stream->Finish();
                return;
            }

            etcdserverpb::WatchResponse resp;
            while (running.load() && stream->Read(&resp)) {
                for (const auto &event : resp.events()) {
                    WatchEvent evt;
                    switch (event.type()) {
                        case mvccpb::Event::PUT:
                            evt.type = WatchEventType::Put;
                            break;
                        case mvccpb::Event::DELETE:
                            evt.type = WatchEventType::Delete;
                            break;
                        default:
                            continue;
                    }
                    if (event.has_kv()) {
                        evt.key = event.kv().key();
                        evt.value = event.kv().value();
                    }
                    if (auto it = watch_callbacks.find(watch_id); it != watch_callbacks.end()) {
                        it->second(evt);
                    }
                }
            }

            stream->WritesDone();
            stream->Finish();
            (void)watch_contexts;
        }));

    return watch_id;
}

void EtcdClient::cancel_watch(WatchId watch_id) {
    std::lock_guard lock(impl_->watch_mutex);

    if (auto it = impl_->watch_running.find(watch_id); it != impl_->watch_running.end()) {
        it->second.store(false);
    }
    // TryCancel 强制中断阻塞中的 stream->Read,使 watch 线程能立即退出
    if (auto it = impl_->watch_contexts.find(watch_id); it != impl_->watch_contexts.end() && it->second) {
        it->second->TryCancel();
    }
    if (auto it = impl_->watch_threads.find(watch_id); it != impl_->watch_threads.end()) {
        if (it->second && it->second->joinable()) {
            it->second->join();
        }
        impl_->watch_threads.erase(it);
    }
    impl_->watch_running.erase(watch_id);
    impl_->watch_callbacks.erase(watch_id);
    impl_->watch_contexts.erase(watch_id);
}

void EtcdClient::cancel_all_watches() {
    std::lock_guard lock(impl_->watch_mutex);

    for (auto &[id, running] : impl_->watch_running) {
        running.store(false);
    }
    for (auto &[id, ctx] : impl_->watch_contexts) {
        if (ctx) {
            ctx->TryCancel();
        }
    }
    for (auto &[id, thread] : impl_->watch_threads) {
        if (thread && thread->joinable()) {
            thread->join();
        }
    }
    impl_->watch_threads.clear();
    impl_->watch_running.clear();
    impl_->watch_callbacks.clear();
    impl_->watch_contexts.clear();
}

} // namespace beast::platform::discovery
