#pragma once

#include "beast/platform/bizutil/math/vector/vec2.hpp"
#include "beast/platform/bizutil/math/vector/vec2_ops.hpp"

#include <cmath>
#include <cstdint>
#include <unordered_map>
#include <utility>
#include <vector>

namespace beast::platform::bizutil::math {

namespace detail {

[[nodiscard]] inline int64_t cell_key(const int cell_x, const int cell_y) noexcept {
    return (static_cast<int64_t>(cell_x) << 32) ^ static_cast<uint32_t>(cell_y);
}

[[nodiscard]] inline int cell_coord(const float value, const float cell_size) noexcept {
    return static_cast<int>(std::floor(value / cell_size));
}

} // namespace detail

template<typename Id>
class HashGrid {
public:
    explicit HashGrid(const float cell_size)
        : cell_size_(cell_size > 0.f ? cell_size : 1.f) {}

    void clear() {
        cells_.clear();
        by_id_.clear();
    }

    void insert(const Id id, const Vec2f position) {
        remove(id);
        const int cell_x = detail::cell_coord(position.x, cell_size_);
        const int cell_y = detail::cell_coord(position.y, cell_size_);
        const int64_t key = detail::cell_key(cell_x, cell_y);
        cells_[key].push_back(Entry{id, position});
        by_id_.emplace(id, CellRef{key, position});
    }

    void remove(const Id id) {
        const auto it = by_id_.find(id);
        if (it == by_id_.end()) {
            return;
        }

        const int64_t key = it->second.key;
        auto cell_it = cells_.find(key);
        if (cell_it != cells_.end()) {
            auto& entries = cell_it->second;
            for (auto entry_it = entries.begin(); entry_it != entries.end(); ++entry_it) {
                if (entry_it->id == id) {
                    entries.erase(entry_it);
                    break;
                }
            }
            if (entries.empty()) {
                cells_.erase(cell_it);
            }
        }
        by_id_.erase(it);
    }

    void update(const Id id, const Vec2f position) { insert(id, position); }

    [[nodiscard]] std::vector<Id> query_radius(const Vec2f center, const float radius) const {
        std::vector<Id> result;
        if (radius <= 0.f) {
            return result;
        }

        const float radius_sq = radius * radius;
        const int min_x = detail::cell_coord(center.x - radius, cell_size_);
        const int max_x = detail::cell_coord(center.x + radius, cell_size_);
        const int min_y = detail::cell_coord(center.y - radius, cell_size_);
        const int max_y = detail::cell_coord(center.y + radius, cell_size_);

        for (int cell_y = min_y; cell_y <= max_y; ++cell_y) {
            for (int cell_x = min_x; cell_x <= max_x; ++cell_x) {
                const auto cell_it = cells_.find(detail::cell_key(cell_x, cell_y));
                if (cell_it == cells_.end()) {
                    continue;
                }
                for (const Entry& entry : cell_it->second) {
                    if (distance_squared(center, entry.position) <= radius_sq) {
                        result.push_back(entry.id);
                    }
                }
            }
        }
        return result;
    }

    [[nodiscard]] float cell_size() const noexcept { return cell_size_; }
    [[nodiscard]] std::size_t size() const noexcept { return by_id_.size(); }

private:
    struct Entry {
        Id id;
        Vec2f position;
    };

    struct CellRef {
        int64_t key;
        Vec2f position;
    };

    float cell_size_;
    std::unordered_map<int64_t, std::vector<Entry>> cells_;
    std::unordered_map<Id, CellRef> by_id_;
};

} // namespace beast::platform::bizutil::math
