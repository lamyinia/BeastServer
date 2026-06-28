#include "beast/client/wire_codec.hpp"

namespace beast::client::wire {

Bytes encode_varint(std::uint64_t value) {
    Bytes out;
    while (true) {
        if ((value & ~0x7FULL) == 0) {
            out.push_back(static_cast<std::uint8_t>(value & 0x7F));
            break;
        }
        out.push_back(static_cast<std::uint8_t>((value & 0x7F) | 0x80));
        value >>= 7;
    }
    return out;
}

VarintResult decode_varint(const Bytes& buffer, std::size_t offset) {
    VarintResult result;
    if (offset >= buffer.size()) {
        return result;
    }

    std::uint64_t value = 0;
    std::uint32_t shift = 0;
    for (std::size_t i = offset; i < buffer.size(); ++i) {
        const std::uint8_t byte = buffer[i];
        value |= static_cast<std::uint64_t>(byte & 0x7F) << shift;
        result.next_offset = i + 1;
        if ((byte & 0x80) == 0) {
            result.ok = true;
            result.value = value;
            return result;
        }
        shift += 7;
        if (shift > 63) {
            break;
        }
    }
    return result;
}

Bytes encode_tag(std::uint32_t field_number, WireType wire_type) {
    return encode_varint((static_cast<std::uint64_t>(field_number) << 3) |
                         (static_cast<std::uint64_t>(wire_type) & 0x07));
}

FieldKey decode_field_key(const Bytes& buffer, std::size_t offset) {
    FieldKey key;
    const VarintResult tag = decode_varint(buffer, offset);
    if (!tag.ok) {
        return key;
    }
    key.ok = true;
    key.next_offset = tag.next_offset;
    key.field_number = static_cast<std::uint32_t>(tag.value >> 3);
    key.wire_type = static_cast<WireType>(tag.value & 0x07);
    return key;
}

Bytes encode_string_field(std::uint32_t field_number, const std::string& value) {
    Bytes out = encode_tag(field_number, WireType::LengthDelimited);
    const Bytes len_bytes = encode_varint(value.size());
    out.insert(out.end(), len_bytes.begin(), len_bytes.end());
    out.insert(out.end(), value.begin(), value.end());
    return out;
}

Bytes encode_bytes_field(std::uint32_t field_number, const Bytes& value) {
    Bytes out = encode_tag(field_number, WireType::LengthDelimited);
    const Bytes len_bytes = encode_varint(value.size());
    out.insert(out.end(), len_bytes.begin(), len_bytes.end());
    out.insert(out.end(), value.begin(), value.end());
    return out;
}

Bytes encode_bool_field(std::uint32_t field_number, bool value) {
    Bytes out = encode_tag(field_number, WireType::Varint);
    const Bytes varint = encode_varint(value ? 1 : 0);
    out.insert(out.end(), varint.begin(), varint.end());
    return out;
}

Bytes encode_uint64_field(std::uint32_t field_number, std::uint64_t value) {
    Bytes out = encode_tag(field_number, WireType::Varint);
    const Bytes varint = encode_varint(value);
    out.insert(out.end(), varint.begin(), varint.end());
    return out;
}

BytesResult decode_length_delimited(const Bytes& buffer, std::size_t offset) {
    BytesResult result;
    const VarintResult len = decode_varint(buffer, offset);
    if (!len.ok) {
        return result;
    }
    const std::size_t start = len.next_offset;
    const std::size_t end = start + static_cast<std::size_t>(len.value);
    if (end > buffer.size()) {
        return result;
    }
    result.ok = true;
    result.next_offset = end;
    result.value.assign(buffer.begin() + static_cast<std::ptrdiff_t>(start),
                        buffer.begin() + static_cast<std::ptrdiff_t>(end));
    return result;
}

StringResult decode_string_field(const Bytes& buffer, std::size_t offset, WireType wire_type) {
    StringResult result;
    if (wire_type != WireType::LengthDelimited) {
        return result;
    }
    const BytesResult chunk = decode_length_delimited(buffer, offset);
    if (!chunk.ok) {
        return result;
    }
    result.ok = true;
    result.next_offset = chunk.next_offset;
    result.value.assign(chunk.value.begin(), chunk.value.end());
    return result;
}

BoolResult decode_bool_field(const Bytes& buffer, std::size_t offset, WireType wire_type) {
    BoolResult result;
    if (wire_type != WireType::Varint) {
        return result;
    }
    const VarintResult val = decode_varint(buffer, offset);
    if (!val.ok) {
        return result;
    }
    result.ok = true;
    result.next_offset = val.next_offset;
    result.value = val.value != 0;
    return result;
}

VarintResult decode_uint64_field(const Bytes& buffer, std::size_t offset, WireType wire_type) {
    if (wire_type != WireType::Varint) {
        return {};
    }
    return decode_varint(buffer, offset);
}

DecodeResult skip_field(const Bytes& buffer, std::size_t offset, WireType wire_type) {
    switch (wire_type) {
    case WireType::Varint:
        return decode_varint(buffer, offset);
    case WireType::Fixed64: {
        DecodeResult result;
        if (offset + 8 <= buffer.size()) {
            result.ok = true;
            result.next_offset = offset + 8;
        }
        return result;
    }
    case WireType::LengthDelimited:
        return decode_length_delimited(buffer, offset);
    case WireType::Fixed32: {
        DecodeResult result;
        if (offset + 4 <= buffer.size()) {
            result.ok = true;
            result.next_offset = offset + 4;
        }
        return result;
    }
    default:
        return {};
    }
}

} // namespace beast::client::wire
