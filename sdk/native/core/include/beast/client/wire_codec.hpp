#pragma once

#include "beast/client/types.hpp"

#include <optional>
#include <vector>

namespace beast::client::wire {

enum class WireType : std::uint32_t {
    Varint = 0,
    Fixed64 = 1,
    LengthDelimited = 2,
    Fixed32 = 5,
};

struct FieldKey {
    std::uint32_t field_number = 0;
    WireType wire_type = WireType::Varint;
    std::size_t next_offset = 0;
    bool ok = false;
};

struct DecodeResult {
    bool ok = false;
    std::size_t next_offset = 0;
};

struct VarintResult : DecodeResult {
    std::uint64_t value = 0;
};

struct BytesResult : DecodeResult {
    Bytes value;
};

struct StringResult : DecodeResult {
    std::string value;
};

struct BoolResult : DecodeResult {
    bool value = false;
};

Bytes encode_varint(std::uint64_t value);
VarintResult decode_varint(const Bytes& buffer, std::size_t offset);

Bytes encode_tag(std::uint32_t field_number, WireType wire_type);
FieldKey decode_field_key(const Bytes& buffer, std::size_t offset);

Bytes encode_string_field(std::uint32_t field_number, const std::string& value);
Bytes encode_bytes_field(std::uint32_t field_number, const Bytes& value);
Bytes encode_bool_field(std::uint32_t field_number, bool value);
Bytes encode_uint64_field(std::uint32_t field_number, std::uint64_t value);

StringResult decode_string_field(const Bytes& buffer, std::size_t offset, WireType wire_type);
BytesResult decode_length_delimited(const Bytes& buffer, std::size_t offset);
BoolResult decode_bool_field(const Bytes& buffer, std::size_t offset, WireType wire_type);
VarintResult decode_uint64_field(const Bytes& buffer, std::size_t offset, WireType wire_type);

DecodeResult skip_field(const Bytes& buffer, std::size_t offset, WireType wire_type);

} // namespace beast::client::wire
