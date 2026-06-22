#pragma once

#include "beast/platform/server/room_service.hpp"

#include <grpcpp/grpcpp.h>

#include <memory>

#include "room.grpc.pb.h"

namespace beast::platform::rpc {

// gRPC RoomService 实现：将 CreateRoom RPC 委托给 server::RoomService::create_room()。
class RoomServiceGrpcImpl final : public ::beast::platform::RoomService::Service {
public:
    explicit RoomServiceGrpcImpl(server::RoomService& room_service)
        : room_service_(room_service) {}

    grpc::Status CreateRoom(
        grpc::ServerContext* context,
        const ::beast::platform::CreateRoomRequest* request,
        ::beast::platform::CreateRoomResponse* response) override;

private:
    server::RoomService& room_service_;
};

} // namespace beast::platform::rpc
