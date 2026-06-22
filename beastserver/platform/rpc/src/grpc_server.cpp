#include "beast/platform/rpc/grpc_server.hpp"

#include "beast/platform/core/log/logger.hpp"

#include <grpcpp/server_builder.h>

#include <string>

namespace beast::platform::rpc {

GrpcServer::GrpcServer(const std::uint16_t port)
    : port_(port) {}

GrpcServer::~GrpcServer() {
    stop();
}

void GrpcServer::register_service(std::shared_ptr<grpc::Service> service) {
    if (!service) {
        return;
    }
    services_.push_back(std::move(service));
}

bool GrpcServer::start(const bool blocking) {
    if (running_) {
        return true;
    }

    grpc::ServerBuilder builder;
    const std::string address = "0.0.0.0:" + std::to_string(port_);
    builder.AddListeningPort(address, grpc::InsecureServerCredentials());

    for (const auto& service : services_) {
        builder.RegisterService(service.get());
    }

    // 同步服务器需要一个被轮询的 CQ
    cq_ = builder.AddCompletionQueue();

    server_ = builder.BuildAndStart();
    if (!server_) {
        BEAST_LOG_ERROR("GrpcServer: failed to start on port {}", port_);
        return false;
    }

    running_ = true;
    BEAST_LOG_INFO("GrpcServer: listening on {}", address);

    if (blocking) {
        server_->Wait();
    }

    return true;
}

void GrpcServer::stop() {
    if (!running_ || !server_) {
        return;
    }

    BEAST_LOG_INFO("GrpcServer: stopping port {}", port_);
    server_->Shutdown();
    if (cq_) {
        cq_->Shutdown();
        // drain remaining events to satisfy CQ invariant
        void* tag = nullptr;
        bool ok = false;
        while (cq_->Next(&tag, &ok)) {
        }
    }
    server_.reset();
    cq_.reset();
    running_ = false;
    BEAST_LOG_INFO("GrpcServer: stopped");
}

void GrpcServer::wait() {
    if (server_) {
        server_->Wait();
    }
}

} // namespace beast::platform::rpc
