#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <variant>

namespace beast::platform::dirtypersist {

// 统一字段类型：覆盖 mongo bson / mysql prepared stmt 都能映射的基础标量。
// 复合类型（数组/嵌套文档）请序列化成 string 存储。
using FieldValue = std::variant<
    std::monostate,      // Null
    std::int64_t,         // Int64（mongo int32/int64 / mysql BIGINT/INT 都映射到 int64）
    double,               // Double
    bool,                 // Bool
    std::string           // String（含序列化后的 JSON / bson 文本）
>;

enum class FieldType : std::uint8_t {
    Null   = 0,
    Int64  = 1,
    Double = 2,
    Bool   = 3,
    String = 4,
};

// 字段元数据：编译期描述，配合 offsetof 使用。
// offset 是相对于 struct 起点的偏移量（offsetof 取得）。
struct FieldMeta {
    std::string_view name;
    FieldType        type;
    std::size_t      offset;
};

// 根据 FieldType 从内存读取出 FieldValue
inline FieldValue read_field_value(const void* base, FieldType type) {
    switch (type) {
        case FieldType::Int64:  return *static_cast<const std::int64_t*>(base);
        case FieldType::Double: return *static_cast<const double*>(base);
        case FieldType::Bool:   return *static_cast<const bool*>(base);
        case FieldType::String: return *static_cast<const std::string*>(base);
        case FieldType::Null:   break;
    }
    return std::monostate{};
}

// 根据 FieldType 把 FieldValue 写回内存
inline void write_field_value(void* base, FieldType type, const FieldValue& v) {
    switch (type) {
        case FieldType::Int64:
            *static_cast<std::int64_t*>(base) = std::get<std::int64_t>(v);
            break;
        case FieldType::Double:
            *static_cast<double*>(base) = std::get<double>(v);
            break;
        case FieldType::Bool:
            *static_cast<bool*>(base) = std::get<bool>(v);
            break;
        case FieldType::String:
            *static_cast<std::string*>(base) = std::get<std::string>(v);
            break;
        case FieldType::Null:
            break;
    }
}

[[nodiscard]] inline const char* field_type_name(FieldType t) noexcept {
    switch (t) {
        case FieldType::Null:   return "null";
        case FieldType::Int64:  return "int64";
        case FieldType::Double: return "double";
        case FieldType::Bool:   return "bool";
        case FieldType::String: return "string";
    }
    return "unknown";
}

} // namespace beast::platform::dirtypersist
