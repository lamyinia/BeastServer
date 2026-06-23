#include "beast/platform/bizutil/config/result.hpp"

namespace beast::platform::bizutil::config {

std::string BizError::to_string() const {
    if (table.empty()) {
        return path + ": " + message;
    }
    if (path.empty()) {
        return table + ": " + message;
    }
    return table + " (" + path + "): " + message;
}

LoadResult LoadResult::success() {
    LoadResult result;
    result.ok = true;
    return result;
}

void LoadResult::add_error(BizError error) {
    ok = false;
    errors.push_back(std::move(error));
}

} // namespace beast::platform::bizutil::config
