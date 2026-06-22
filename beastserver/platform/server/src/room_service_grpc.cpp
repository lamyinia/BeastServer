#include "beast/platform/server/room_service_grpc.hpp"

#include "beast/platform/core/log/logger.hpp"

namespace beast::platform::rpc {

grpc::Status RoomServiceGrpcImpl::CreateRoom(
    grpc::ServerContext* /*context*/,
    const ::beast::platform::CreateRoomRequest* request,
    ::beast::platform::CreateRoomResponse* response) {

    if (request->engine_name().empty()) {
        return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, "engine_name is required");
    }

    server::CreateRoomParams params;
    params.engine_name = request->engine_name();
    params.instance_id = request->instance_id();
    params.player_ids.reserve(static_cast<std::size_t>(request->player_ids_size()));
    for (const auto& pid : request->player_ids()) {
        params.player_ids.push_back(pid);
    }

    const auto outcome = room_service_.create_room(std::move(params));
    if (!outcome.ok) {
        BEAST_LOG_WARN("gRPC CreateRoom failed: {}", outcome.error_message);
        return grpc::Status(grpc::StatusCode::INTERNAL, outcome.error_message);
    }

    response->set_instance_id(outcome.instance_id);
    response->set_engine_name(outcome.engine_name);
    BEAST_LOG_INFO("gRPC CreateRoom ok: instance_id={}", outcome.instance_id);
    return grpc::Status::OK;
}

} // namespace beast::platform::rpc
