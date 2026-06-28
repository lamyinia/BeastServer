#include "beast/client/frame_codec.hpp"
#include "beast/client/message_codec.hpp"
#include "beast/client/routes.hpp"
#include "beast/client/wire_codec.hpp"

#include "../tests/test_main.hpp"

using namespace beast::client;

namespace {

Bytes encode_auth_response(const AuthResponse& response) {
    using namespace wire;
    Bytes out;
    const Bytes success_field = encode_bool_field(1, response.success);
    out.insert(out.end(), success_field.begin(), success_field.end());
    if (!response.message.empty()) {
        const Bytes field = encode_string_field(2, response.message);
        out.insert(out.end(), field.begin(), field.end());
    }
    if (response.pid != 0) {
        const Bytes field = encode_uint64_field(3, response.pid);
        out.insert(out.end(), field.begin(), field.end());
    }
    return out;
}

} // namespace

static void test_auth_request_encode() {
    AuthRequest request;
    request.token = "dev:42";
    request.device_id = "native";
    request.version = "1.0";

    const Bytes encoded = auth_request_to_bytes(request);
    EXPECT_TRUE(!encoded.empty());
}

static void test_auth_response_roundtrip() {
    AuthResponse response;
    response.success = true;
    response.message = "ok";
    response.pid = 42;

    const Bytes encoded = encode_auth_response(response);
    const std::optional<AuthResponse> parsed = auth_response_from_bytes(encoded);
    EXPECT_TRUE(parsed.has_value());
    EXPECT_TRUE(parsed->success);
    EXPECT_EQ(parsed->message, std::string("ok"));
    EXPECT_EQ(parsed->pid, 42ULL);
}

static void test_envelope_frame() {
    AuthRequest request;
    request.token = "dev:42";
    request.device_id = "d";
    request.version = "1";

    const Bytes frame = encode_frame(routes::kAuthLogin, auth_request_to_bytes(request), 7);
    EXPECT_TRUE(!frame.empty());

    const FrameDecodeResult split = frame_try_decode(frame);
    EXPECT_EQ(split.frames.size(), 1U);

    const std::optional<Envelope> envelope = decode_frame_body(split.frames[0]);
    EXPECT_TRUE(envelope.has_value());
    EXPECT_EQ(envelope->route, std::string(routes::kAuthLogin));
    EXPECT_EQ(envelope->client_seq, 7ULL);
}

int main() {
    run_test("auth_request_encode", test_auth_request_encode);
    run_test("auth_response_roundtrip", test_auth_response_roundtrip);
    run_test("envelope_frame", test_envelope_frame);

    if (g_tests_failed == 0) {
        std::cout << "All message codec tests passed\n";
        return 0;
    }
    std::cout << g_tests_failed << " test(s) failed\n";
    return 1;
}
