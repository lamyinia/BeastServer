#pragma once

#include "beast/platform/bizutil/config/result.hpp"

#include <filesystem>

namespace google::protobuf {
class Message;
}

namespace beast::platform::bizutil::config {

class Loader {
public:
    [[nodiscard]] static bool load_pb_file(
        const std::filesystem::path& path,
        google::protobuf::Message& out,
        BizError* error = nullptr);
};

} // namespace beast::platform::bizutil::config
