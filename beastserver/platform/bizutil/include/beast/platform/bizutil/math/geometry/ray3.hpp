#pragma once

#include "beast/platform/bizutil/math/geometry/aabb3.hpp"
#include "beast/platform/bizutil/math/geometry/sphere.hpp"
#include "beast/platform/bizutil/math/vector/vec3.hpp"
#include "beast/platform/bizutil/math/vector/vec3_ops.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

namespace beast::platform::bizutil::math {

// 射线：origin + t * direction，direction 应为单位向量。
struct Ray3f {
    Vec3f origin{};
    Vec3f direction{1.f, 0.f, 0.f};

    constexpr Ray3f() = default;
    constexpr Ray3f(const Vec3f origin_, const Vec3f direction_)
        : origin(origin_)
        , direction(direction_) {}

    [[nodiscard]] Vec3f at(const float t) const noexcept { return origin + scale(direction, t); }
};

// 射线 × AABB（slab 法）：命中返回最近的 t（>= 0），否则 nullopt。
[[nodiscard]] inline std::optional<float> intersect(const Ray3f& ray, const Aabb3f& box) noexcept {
    if (box.empty()) {
        return std::nullopt;
    }

    float tmin = 0.f;
    float tmax = std::numeric_limits<float>::max();

    const float origin[3] = {ray.origin.x, ray.origin.y, ray.origin.z};
    const float dir[3] = {ray.direction.x, ray.direction.y, ray.direction.z};
    const float lo[3] = {box.min.x, box.min.y, box.min.z};
    const float hi[3] = {box.max.x, box.max.y, box.max.z};

    for (int axis = 0; axis < 3; ++axis) {
        if (std::fabs(dir[axis]) < 1e-8f) {
            if (origin[axis] < lo[axis] || origin[axis] > hi[axis]) {
                return std::nullopt;
            }
            continue;
        }
        const float inv = 1.f / dir[axis];
        float t1 = (lo[axis] - origin[axis]) * inv;
        float t2 = (hi[axis] - origin[axis]) * inv;
        if (t1 > t2) {
            std::swap(t1, t2);
        }
        tmin = std::max(tmin, t1);
        tmax = std::min(tmax, t2);
        if (tmin > tmax) {
            return std::nullopt;
        }
    }
    return tmin;
}

// 射线 × 球：命中返回最近的非负 t，否则 nullopt（假定 direction 已归一化）。
[[nodiscard]] inline std::optional<float> intersect(const Ray3f& ray, const Spheref& sphere) noexcept {
    if (sphere.empty()) {
        return std::nullopt;
    }

    const Vec3f oc = ray.origin - sphere.center;
    const float b = dot(oc, ray.direction);
    const float c = dot(oc, oc) - sphere.radius * sphere.radius;
    const float disc = b * b - c;
    if (disc < 0.f) {
        return std::nullopt;
    }

    const float sqrt_disc = std::sqrt(disc);
    float t = -b - sqrt_disc;
    if (t < 0.f) {
        t = -b + sqrt_disc;
    }
    if (t < 0.f) {
        return std::nullopt;
    }
    return t;
}

} // namespace beast::platform::bizutil::math
