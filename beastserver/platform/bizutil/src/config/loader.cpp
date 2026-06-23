#include "beast/platform/bizutil/config/loader.hpp"

#include <fstream>

#include <google/protobuf/message.h>

namespace beast::platform::bizutil::config {

bool Loader::load_pb_file(
    const std::filesystem::path& path,
    google::protobuf::Message& out,
    BizError* error) {
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) {
        if (error != nullptr) {
            *error = BizError{
                .table = {},
                .path = path.string(),
                .message = "open failed",
            };
        }
        return false;
    }

    if (!out.ParseFromIstream(&input)) {
        if (error != nullptr) {
            *error = BizError{
                .table = {},
                .path = path.string(),
                .message = "protobuf parse failed",
            };
        }
        return false;
    }

    return true;
}

} // namespace beast::platform::bizutil::config
