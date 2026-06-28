#include "beast/client/message_codec.hpp"

#include "beast/client/frame_codec.hpp"
#include "beast/client/wire_codec.hpp"

namespace beast::client {

using namespace wire;

Bytes envelope_to_bytes(const Envelope& envelope) {
    Bytes out;
    if (!envelope.route.empty()) {
        const Bytes field = encode_string_field(1, envelope.route);
        out.insert(out.end(), field.begin(), field.end());
    }
    if (!envelope.payload.empty()) {
        const Bytes field = encode_bytes_field(2, envelope.payload);
        out.insert(out.end(), field.begin(), field.end());
    }
    if (envelope.client_seq != 0) {
        const Bytes field = encode_uint64_field(3, envelope.client_seq);
        out.insert(out.end(), field.begin(), field.end());
    }
    return out;
}

std::optional<Envelope> envelope_from_bytes(const Bytes& data) {
    if (data.empty()) {
        return std::nullopt;
    }

    Envelope envelope;
    std::size_t offset = 0;
    while (offset < data.size()) {
        const FieldKey key = decode_field_key(data, offset);
        if (!key.ok) {
            return std::nullopt;
        }
        offset = key.next_offset;

        switch (key.field_number) {
        case 1: {
            const StringResult parsed = decode_string_field(data, offset, key.wire_type);
            if (!parsed.ok) {
                return std::nullopt;
            }
            envelope.route = parsed.value;
            offset = parsed.next_offset;
            break;
        }
        case 2: {
            const BytesResult parsed = decode_length_delimited(data, offset);
            if (!parsed.ok) {
                return std::nullopt;
            }
            envelope.payload = parsed.value;
            offset = parsed.next_offset;
            break;
        }
        case 3: {
            const VarintResult parsed = decode_uint64_field(data, offset, key.wire_type);
            if (!parsed.ok) {
                return std::nullopt;
            }
            envelope.client_seq = parsed.value;
            offset = parsed.next_offset;
            break;
        }
        default: {
            const DecodeResult skipped = skip_field(data, offset, key.wire_type);
            if (!skipped.ok) {
                return std::nullopt;
            }
            offset = skipped.next_offset;
            break;
        }
        }
    }
    return envelope;
}

Bytes auth_request_to_bytes(const AuthRequest& request) {
    Bytes out;
    if (!request.token.empty()) {
        const Bytes field = encode_string_field(1, request.token);
        out.insert(out.end(), field.begin(), field.end());
    }
    if (!request.device_id.empty()) {
        const Bytes field = encode_string_field(2, request.device_id);
        out.insert(out.end(), field.begin(), field.end());
    }
    if (!request.version.empty()) {
        const Bytes field = encode_string_field(3, request.version);
        out.insert(out.end(), field.begin(), field.end());
    }
    return out;
}

std::optional<AuthResponse> auth_response_from_bytes(const Bytes& data) {
    if (data.empty()) {
        return std::nullopt;
    }

    AuthResponse response;
    std::size_t offset = 0;
    while (offset < data.size()) {
        const FieldKey key = decode_field_key(data, offset);
        if (!key.ok) {
            return std::nullopt;
        }
        offset = key.next_offset;

        switch (key.field_number) {
        case 1: {
            const BoolResult parsed = decode_bool_field(data, offset, key.wire_type);
            if (!parsed.ok) {
                return std::nullopt;
            }
            response.success = parsed.value;
            offset = parsed.next_offset;
            break;
        }
        case 2:
        case 4: {
            const StringResult parsed = decode_string_field(data, offset, key.wire_type);
            if (!parsed.ok) {
                return std::nullopt;
            }
            if (key.field_number == 2) {
                response.message = parsed.value;
            } else {
                response.nickname = parsed.value;
            }
            offset = parsed.next_offset;
            break;
        }
        case 3: {
            const VarintResult parsed = decode_uint64_field(data, offset, key.wire_type);
            if (!parsed.ok) {
                return std::nullopt;
            }
            response.pid = parsed.value;
            offset = parsed.next_offset;
            break;
        }
        default: {
            const DecodeResult skipped = skip_field(data, offset, key.wire_type);
            if (!skipped.ok) {
                return std::nullopt;
            }
            offset = skipped.next_offset;
            break;
        }
        }
    }
    return response;
}

Bytes encode_frame(const std::string& route, const Bytes& payload, const std::uint64_t client_seq) {
    Envelope envelope;
    envelope.route = route;
    envelope.payload = payload;
    envelope.client_seq = client_seq;
    return frame_encode(envelope_to_bytes(envelope));
}

std::optional<Envelope> decode_frame_body(const Bytes& frame_body) {
    return envelope_from_bytes(frame_body);
}

} // namespace beast::client
