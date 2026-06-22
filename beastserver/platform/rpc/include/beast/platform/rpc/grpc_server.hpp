#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include <grpcpp/completion_queue.h>
#include <grpcpp/server.h>

namespace beast::platform::rpc {

// gRPC 服务端封装：服务注册、启动、停止。
class GrpcServer {
public:
    explicit GrpcServer(std::uint16_t port);

    ~GrpcServer();

    GrpcServer(const GrpcServer&) = delete;
    GrpcServer& operator=(const GrpcServer&) = delete;

    void register_service(std::shared_ptr<grpc::Service> service);

    // blocking=true 时在当前线程 Wait()；默认非阻塞，BuildAndStart 后在 gRPC 线程池运行。
    [[nodiscard]] bool start(bool blocking = false);

    void stop();

    [[nodiscard]] bool is_running() const noexcept { return running_; }

    void wait();

private:
    std::uint16_t port_;
    std::vector<std::shared_ptr<grpc::Service>> services_;
    std::unique_ptr<grpc::Server> server_;
    std::unique_ptr<grpc::ServerCompletionQueue> cq_;
    bool running_{false};
};

} // namespace beast::platform::rpc
