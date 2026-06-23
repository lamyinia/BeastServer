#pragma once

namespace beast::platform::bizutil::config {

struct LoadOptions {
    bool verify_hash{false};
    bool use_manifest{true};
    bool fail_on_missing{true};
};

} // namespace beast::platform::bizutil::config
