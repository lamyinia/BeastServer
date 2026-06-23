#pragma once

#include <string>
#include <vector>

namespace beast::platform::bizutil::config {

struct BizError {
    std::string table;
    std::string path;
    std::string message;

    [[nodiscard]] std::string to_string() const;
};

struct LoadResult {
    bool ok{false};
    std::vector<BizError> errors;

    [[nodiscard]] static LoadResult success();
    void add_error(BizError error);
};

} // namespace beast::platform::bizutil::config
