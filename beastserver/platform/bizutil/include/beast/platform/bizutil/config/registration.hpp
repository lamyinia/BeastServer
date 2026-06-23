#pragma once

#include <functional>
#include <memory>
#include <string>

namespace google::protobuf {
class Message;
}

namespace beast::platform::bizutil::config {

struct BizTableRegistration {
    std::string logical_name;
    std::function<std::unique_ptr<google::protobuf::Message>()> factory;
};

} // namespace beast::platform::bizutil::config
