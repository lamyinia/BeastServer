#pragma once

#include "beast/mixin/dirtypersist/field_value.hpp"

#include <cstddef>
#include <concepts>
#include <string>
#include <utility>
#include <vector>

namespace beast::platform::dirtypersist {

// ============================================================================
// EntityTraits<T> — 用户必须特化才能让 T 通过 Persistable concept
//
// 用户特化时只需提供 metadata（kTable / kIdColumn / kIdOffset / kIdType / kFields），
// get_field / set_field / diff / get_id 等访问函数由 free function template 自动提供
// （通过 ADL 查找，无需用户写实现）。
//
// 用法示例（注意：特化必须在文件作用域打开 beast::platform::dirtypersist 命名空间，
// 不能在匿名命名空间内用全限定名 `struct beast::platform::dirtypersist::EntityTraits<T>` ——
// C++ 标准要求特化必须放在被特化模板所属的命名空间中声明）：
//
//   struct PlayerEntity {
//       std::string  uid;
//       std::int64_t hp;
//       std::int64_t gold;
//       std::string  name;
//   };
//
//   namespace beast::platform::dirtypersist {
//   template<>
//   struct EntityTraits<PlayerEntity> {
//       using entity_type = PlayerEntity;
//       static constexpr std::string_view kTable    = "players";
//       static constexpr std::string_view kIdColumn = "uid";
//       static constexpr std::size_t      kIdOffset = offsetof(PlayerEntity, uid);
//       static constexpr FieldType        kIdType   = FieldType::String;
//       static constexpr FieldMeta        kFields[] = {
//           BEAST_PERSIST_FIELD(hp,   Int64),
//           BEAST_PERSIST_FIELD(gold, Int64),
//           BEAST_PERSIST_FIELD(name, String),
//       };
//   };
//   } // namespace beast::platform::dirtypersist
// ============================================================================

template<typename T>
struct EntityTraits;  // 未特化 = 不可持久化（Persistable concept 会拒绝）

// Concept: 检查 EntityTraits<T> 是否已正确特化。
// 这是"struct require 接口"的实现：未特化或字段不全的 T 无法通过编译。
template<typename T>
concept Persistable = requires {
    typename EntityTraits<T>;
    requires std::is_same_v<typename EntityTraits<T>::entity_type, T>;
    { EntityTraits<T>::kTable }    -> std::convertible_to<std::string_view>;
    { EntityTraits<T>::kIdColumn } -> std::convertible_to<std::string_view>;
    { EntityTraits<T>::kIdOffset } -> std::convertible_to<std::size_t>;
    { EntityTraits<T>::kIdType }   -> std::convertible_to<FieldType>;
    { EntityTraits<T>::kFields }   -> std::ranges::range;
};

// ============================================================================
// 通用访问函数（自动通过 ADL 调用，用户无需实现）
// ============================================================================

template<Persistable T>
[[nodiscard]] inline FieldValue get_field(const T& e, std::size_t idx) {
    const auto& meta = EntityTraits<T>::kFields[idx];
    const char* base = reinterpret_cast<const char*>(&e);
    return read_field_value(base + meta.offset, meta.type);
}

template<Persistable T>
inline void set_field(T& e, std::size_t idx, const FieldValue& v) {
    const auto& meta = EntityTraits<T>::kFields[idx];
    char* base = reinterpret_cast<char*>(&e);
    write_field_value(base + meta.offset, meta.type, v);
}

// diff 两个 entity，输出 (field_idx, new_value) 列表
template<Persistable T>
inline void diff(const T& old_v, const T& new_v,
                 std::vector<std::pair<std::size_t, FieldValue>>& out) {
    const auto& fields = EntityTraits<T>::kFields;
    for (std::size_t i = 0; i < std::size(fields); ++i) {
        const auto va = get_field(old_v, i);
        const auto vb = get_field(new_v, i);
        if (va != vb) {
            out.emplace_back(i, vb);
        }
    }
}

// 主键转字符串（统一 backend upsert key 类型）
template<Persistable T>
[[nodiscard]] inline std::string get_id(const T& e) {
    const char* base = reinterpret_cast<const char*>(&e);
    const auto offset = EntityTraits<T>::kIdOffset;
    switch (EntityTraits<T>::kIdType) {
        case FieldType::Int64:
            return std::to_string(*reinterpret_cast<const std::int64_t*>(base + offset));
        case FieldType::String:
            return *reinterpret_cast<const std::string*>(base + offset);
        case FieldType::Null:
        case FieldType::Double:
        case FieldType::Bool:
            break;
    }
    return {};
}

// 把 entity 全部字段转成 (name, value) 序列（用于新插入场景）
template<Persistable T>
[[nodiscard]] inline std::vector<std::pair<std::string, FieldValue>>
flatten(const T& e) {
    const auto& fields = EntityTraits<T>::kFields;
    std::vector<std::pair<std::string, FieldValue>> out;
    out.reserve(std::size(fields));
    for (std::size_t i = 0; i < std::size(fields); ++i) {
        out.emplace_back(std::string{fields[i].name}, get_field(e, i));
    }
    return out;
}

// 根据 field name 查找 idx（线性查找，字段少时足够）
template<Persistable T>
[[nodiscard]] inline std::size_t find_field_idx(std::string_view name) {
    const auto& fields = EntityTraits<T>::kFields;
    for (std::size_t i = 0; i < std::size(fields); ++i) {
        if (fields[i].name == name) return i;
    }
    return static_cast<std::size_t>(-1);
}

} // namespace beast::platform::dirtypersist

// ============================================================================
// 用户侧辅助宏：单字段注册一行
// 用法见 entity_traits.hpp 文件顶部的示例
// ============================================================================
#define BEAST_PERSIST_FIELD(Member, FType)                                 \
    ::beast::platform::dirtypersist::FieldMeta{                            \
        #Member,                                                            \
        ::beast::platform::dirtypersist::FieldType::FType,                 \
        offsetof(entity_type, Member)                                       \
    }
