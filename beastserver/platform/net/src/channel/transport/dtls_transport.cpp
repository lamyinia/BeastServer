#include "beast/platform/net/transport/dtls_transport.hpp"

#include "beast/platform/core/log/logger.hpp"

#include <boost/asio/post.hpp>
#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/ssl.h>

#include <cerrno>
#include <cstring>
#include <utility>

namespace beast::platform::net::transport {

namespace {

/// 把 OpenSSL 错误栈中的错误转成字符串（用于日志和回调）。
std::string get_openssl_error() {
    std::string result;
    unsigned long err = 0;
    while ((err = ERR_get_error()) != 0) {
        char buf[256] = {0};
        ERR_error_string_n(err, buf, sizeof(buf));
        if (!result.empty()) {
            result += "; ";
        }
        result += buf;
    }
    if (result.empty()) {
        result = "unknown openssl error";
    }
    return result;
}

} // namespace

DtlsTransport::DtlsTransport(
    Strand strand,
    SslContextPtr ssl_ctx,
    std::uint32_t handshake_timeout_seconds)
    : strand_(std::move(strand))
    , ssl_ctx_(std::move(ssl_ctx))
    , handshake_timeout_seconds_(handshake_timeout_seconds == 0 ? 5 : handshake_timeout_seconds)
    , handshake_timer_(strand_) {
    BOOST_ASSERT(ssl_ctx_ && "DtlsTransport: ssl_ctx must not be null");
}

DtlsTransport::~DtlsTransport() {
    if (!closed_) {
        do_close("dtls transport destroyed");
    }
    if (ssl_) {
        SSL_free(ssl_);
        ssl_ = nullptr;
    }
    // read_bio_/write_bio_ 由 SSL_free 内部释放（SSL_set_bio 后所有权转移）
    BEAST_LOG_DEBUG("DtlsTransport destroyed");
}

DtlsTransport::SslContextPtr DtlsTransport::build_dtls_context(
    const std::string& cert_path,
    const std::string& key_path,
    const std::string& min_version,
    const std::string& cipher_list) {
    // 创建原生 SSL_CTX（DTLS_server_method）。Boost.Asio 的 ssl::context 不支持 DTLS，
    // 必须直接用 OpenSSL API。
    SSL_CTX* raw_ctx = SSL_CTX_new(DTLS_server_method());
    if (!raw_ctx) {
        BEAST_LOG_ERROR("DtlsTransport: SSL_CTX_new(DTLS_server_method) failed: {}", get_openssl_error());
        return nullptr;
    }
    // 用 shared_ptr + 自定义 deleter 包装，确保 SSL_CTX_free 在最后一个引用释放时调用
    auto ctx = SslContextPtr(raw_ctx, [](SSL_CTX* p) {
        if (p) {
            SSL_CTX_free(p);
        }
    });

    // 最低 DTLS 版本（默认 DTLSv1.2，可选 DTLSv1.3）
    // 注意：DTLS 1.3 需要 OpenSSL 3.2+，旧版本（如 3.0.x）不支持。
    // 用 #ifdef DTLS1_3_VERSION 做编译时检测，避免硬编码宏导致编译错误。
    if (min_version == "DTLSv1.3") {
#ifdef DTLS1_3_VERSION
        if (SSL_CTX_set_min_proto_version(ctx.get(), DTLS1_3_VERSION) != 1) {
            BEAST_LOG_ERROR("DtlsTransport: SSL_CTX_set_min_proto_version(DTLS1_3) failed: {}", get_openssl_error());
            return nullptr;
        }
#else
        BEAST_LOG_ERROR("DtlsTransport: DTLSv1.3 requested but OpenSSL build does not support it "
                        "(requires OpenSSL 3.2+, current={})",
                        OpenSSL_version(OPENSSL_VERSION));
        return nullptr;
#endif
    } else {
        if (SSL_CTX_set_min_proto_version(ctx.get(), DTLS1_2_VERSION) != 1) {
            BEAST_LOG_ERROR("DtlsTransport: SSL_CTX_set_min_proto_version(DTLS1_2) failed: {}", get_openssl_error());
            return nullptr;
        }
    }

    // cipher suite（留空用 OpenSSL 默认）
    if (!cipher_list.empty()) {
        if (SSL_CTX_set_cipher_list(ctx.get(), cipher_list.c_str()) != 1) {
            BEAST_LOG_ERROR("DtlsTransport: SSL_CTX_set_cipher_list failed: {}", cipher_list);
            return nullptr;
        }
    }

    // 加载服务端证书链
    if (SSL_CTX_use_certificate_chain_file(ctx.get(), cert_path.c_str()) != 1) {
        BEAST_LOG_ERROR("DtlsTransport: load certificate failed: {} (path={})",
                        get_openssl_error(), cert_path);
        return nullptr;
    }
    // 加载私钥
    if (SSL_CTX_use_PrivateKey_file(ctx.get(), key_path.c_str(), SSL_FILETYPE_PEM) != 1) {
        BEAST_LOG_ERROR("DtlsTransport: load private key failed: {} (path={})",
                        get_openssl_error(), key_path);
        return nullptr;
    }
    // 校验私钥与证书匹配
    if (SSL_CTX_check_private_key(ctx.get()) != 1) {
        BEAST_LOG_ERROR("DtlsTransport: private key does not match certificate: {}", get_openssl_error());
        return nullptr;
    }

    // DTLS 特有：禁用 session caching（每个 peer 独立 session，cache 无意义且消耗内存）
    SSL_CTX_set_session_cache_mode(ctx.get(), SSL_SESS_CACHE_OFF);
    // 启用 partial write（避免 SSL_write 阻塞）
    SSL_CTX_set_mode(ctx.get(), SSL_MODE_ENABLE_PARTIAL_WRITE);
    // 启用空片段（可提高小数据包的加密强度，DTLS 默认禁用，按需开启）
    // SSL_CTX_set_mode(ctx.get(), SSL_MODE_RELEASE_BUFFERS);

    BEAST_LOG_INFO("DtlsTransport DTLS context built: cert={}, key={}, min_version={}",
                   cert_path, key_path, min_version);
    return ctx;
}

void DtlsTransport::async_handshake(OnHandshakeDone on_done, OnError on_error) {
    boost::asio::post(strand_, [self = shared_from_this(), this,
                                 on_done = std::move(on_done),
                                 on_error = std::move(on_error)]() mutable {
        if (closed_) {
            return;
        }
        if (!udp_output_) {
            BEAST_LOG_ERROR("DtlsTransport async_handshake called without udp_output set");
            if (on_error) {
                on_error("udp_output not set");
            }
            return;
        }
        on_handshake_done_ = std::move(on_done);
        on_error_ = std::move(on_error);

        // 创建 SSL 对象 + 内存 BIO
        ssl_ = SSL_new(ssl_ctx_.get());
        if (!ssl_) {
            on_handshake_failure("SSL_new failed: " + get_openssl_error());
            return;
        }

        read_bio_ = BIO_new(BIO_s_mem());
        write_bio_ = BIO_new(BIO_s_mem());
        if (!read_bio_ || !write_bio_) {
            on_handshake_failure("BIO_new failed: " + get_openssl_error());
            return;
        }

        // SSL_set_bio 后 read_bio/write_bio 的所有权转移到 ssl_，SSL_free 时一起释放
        SSL_set_bio(ssl_, read_bio_, write_bio_);

        // 设置为服务端模式（接收 ClientHello）
        SSL_set_accept_state(ssl_);

        // 启动握手超时定时器
        start_handshake_timer();

        // 如果 inject_inbound 已经在 strand 队列里积累了首包，这里先尝试驱动一次
        // （通常首包会通过 inject_inbound 在 async_handshake 之后到达，
        //  drive_handshake 在 inject_inbound 内部调用）
        handshake_in_progress_ = true;
        drive_handshake();
    });
}

void DtlsTransport::inject_inbound(Bytes&& data) {
    boost::asio::post(strand_, [self = shared_from_this(), this, data = std::move(data)]() mutable {
        if (closed_) {
            return;
        }

        // 防御：async_handshake 未调用前 read_bio_ 为 nullptr
        if (!read_bio_ || !ssl_) {
            BEAST_LOG_WARN("DtlsTransport: inject_inbound called before async_handshake, "
                           "size={} (dropped)", data.size());
            return;
        }

        // 把 UDP 数据喂入 OpenSSL read_bio
        const int written = BIO_write(read_bio_, data.data(), static_cast<int>(data.size()));
        if (written <= 0) {
            BEAST_LOG_WARN("DtlsTransport: BIO_write failed, size={}", data.size());
            return;
        }

        if (!handshake_done_) {
            // 握手阶段：推进握手状态机
            drive_handshake();
        } else {
            // 握手完成：解密所有 pending 数据
            decrypt_pending();
        }
    });
}

/// 解密 read_bio 里的所有 pending 数据，投递给 on_decrypted_。
/// 握手成功后由 inject_inbound / do_udp_read 调用。
void DtlsTransport::decrypt_pending() {
    while (true) {
        const int n = SSL_read(ssl_, decrypt_buf_.data(), static_cast<int>(decrypt_buf_.size()));
        if (n > 0) {
            if (on_decrypted_) {
                Bytes plaintext(decrypt_buf_.begin(), decrypt_buf_.begin() + n);
                on_decrypted_(std::move(plaintext));
            }
            continue;
        }

        const int err = SSL_get_error(ssl_, n);
        switch (err) {
        case SSL_ERROR_WANT_READ:
        case SSL_ERROR_WANT_WRITE:
            // 正常：BIO 里没有更多待解密数据，等下一次 inject_inbound
            break;
        case SSL_ERROR_ZERO_RETURN:
            // 对端发 close_notify，正常关闭
            do_close("remote closed (close_notify)");
            break;
        case SSL_ERROR_SYSCALL:
            // 系统错误：EAGAIN/EWOULDBLOCK 是正常的（BIO 非阻塞），其他视为致命
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            BEAST_LOG_WARN("DtlsTransport: SSL_read syscall error: errno={} ({})",
                           errno, std::strerror(errno));
            do_close("ssl_read syscall error");
            break;
        default:
            // SSL_ERROR_SSL 或其他：致命错误（含 SSL_R_SHUTDOWN_WHILE_IN_INIT 等）
            BEAST_LOG_WARN("DtlsTransport: SSL_read error: {} ({})",
                           ERR_error_string(err, nullptr), get_openssl_error());
            do_close("ssl_read error");
            break;
        }
        break;
    }
}

void DtlsTransport::drive_handshake() {
    if (!ssl_ || handshake_done_ || closed_) {
        return;
    }

    // SSL_do_handshake 在非阻塞模式下：
    //   返回 1 = 握手成功
    //   返回 <0 = 需要 WANT_READ/WANT_WRITE（看 SSL_get_error）
    //   返回 0 = 致命错误
    const int ret = SSL_do_handshake(ssl_);
    if (ret == 1) {
        // 握手成功：必须先 flush write_bio 里的最终 Finished/ChangeCipherSpec 报文，
        // 否则对端永远收不到最后的握手消息，其 handshake 会卡住。
        flush_write_bio();
        handshake_timer_.cancel();
        on_handshake_success();
        return;
    }

    const int err = SSL_get_error(ssl_, ret);
    if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) {
        // 正常状态：等待更多数据或等待 socket 可写
        // 把 write_bio 里 OpenSSL 准备好的响应包发出去
        flush_write_bio();
        return;
    }

    // 致命错误
    on_handshake_failure("SSL_do_handshake failed: " + get_openssl_error());
}

void DtlsTransport::flush_write_bio() {
    // 从 write_bio 取出所有待发的加密数据，逐包调 udp_output_ 投递
    // （udp_output_ 通常是 listener->send_to，自带 strand 串行化）
    while (true) {
        Bytes packet(4096);
        const int n = BIO_read(write_bio_, packet.data(), static_cast<int>(packet.size()));
        if (n <= 0) {
            break;
        }
        packet.resize(static_cast<std::size_t>(n));
        if (udp_output_) {
            udp_output_(std::move(packet));
        }
    }
}

void DtlsTransport::encrypt_and_send(Bytes&& plaintext) {
    boost::asio::post(strand_, [self = shared_from_this(), this,
                                 data = std::move(plaintext)]() mutable {
        if (closed_ || !ssl_ || !handshake_done_) {
            BEAST_LOG_WARN("DtlsTransport: encrypt_and_send called before handshake done");
            return;
        }
        const int written = SSL_write(ssl_, data.data(), static_cast<int>(data.size()));
        if (written <= 0) {
            const int err = SSL_get_error(ssl_, written);
            if (err != SSL_ERROR_WANT_READ && err != SSL_ERROR_WANT_WRITE) {
                BEAST_LOG_WARN("DtlsTransport: SSL_write error: {}", get_openssl_error());
                do_close("ssl_write error");
                return;
            }
        }
        // 取出加密后的数据并发送
        flush_write_bio();
    });
}

void DtlsTransport::set_remote_endpoint(Endpoint endpoint) {
    boost::asio::post(strand_, [self = shared_from_this(), this, endpoint = std::move(endpoint)]() mutable {
        remote_ = std::move(endpoint);
        BEAST_LOG_DEBUG("DtlsTransport remote set to {}:{}", remote_.address().to_string(), remote_.port());
    });
}

void DtlsTransport::on_handshake_success() {
    handshake_done_ = true;
    handshake_in_progress_ = false;
    BEAST_LOG_INFO("DtlsTransport handshake success: remote={}:{}",
                   remote_.address().to_string(), remote_.port());

    // 不启动 do_udp_read：UdpListener 统一收包，demux 后调用 inject_inbound
    // 这样 DTLS 模式与明文模式架构一致

    // 延后一拍执行 on_handshake_done_：确保 OpenSSL 内部状态机完全转换（离开 TLS_ST_BEFORE），
    // 避免紧接着的 decrypt_pending() → SSL_read 在 init 状态下报 SSL_R_SHUTDOWN_WHILE_IN_INIT。
    // post 到 strand_ 保证：①与 decrypt_pending 串行；②此 lambda 返回后才执行回调，
    //   给 OpenSSL 完成状态切换的机会。
    if (on_handshake_done_) {
        boost::asio::post(strand_, [self = shared_from_this(), this]() {
            if (!closed_ && on_handshake_done_) {
                on_handshake_done_();
            }
        });
    }
}

void DtlsTransport::on_handshake_failure(const std::string& reason) {
    handshake_in_progress_ = false;
    BEAST_LOG_WARN("DtlsTransport handshake failed: {}", reason);
    if (on_error_) {
        on_error_(reason);
    }
    do_close("handshake failed: " + reason);
}

void DtlsTransport::start_handshake_timer() {
    handshake_timer_.expires_after(std::chrono::seconds(handshake_timeout_seconds_));
    handshake_timer_.async_wait(
        boost::asio::bind_executor(strand_, [self = shared_from_this(), this](const boost::system::error_code& ec) {
            on_handshake_timeout(ec);
        }));
}

void DtlsTransport::on_handshake_timeout(const boost::system::error_code& ec) {
    if (ec == boost::asio::error::operation_aborted) {
        // 定时器被取消（通常因为握手已完成）
        return;
    }
    if (closed_ || handshake_done_) {
        return;
    }
    on_handshake_failure("handshake timeout (" + std::to_string(handshake_timeout_seconds_) + "s)");
}

void DtlsTransport::close() {
    boost::asio::post(strand_, [self = shared_from_this(), this]() {
        do_close("client closed");
    });
}

void DtlsTransport::do_close(const std::string& reason) {
    if (closed_) {
        return;
    }
    closed_ = true;
    handshake_in_progress_ = false;

    boost::system::error_code ignored;
    handshake_timer_.cancel(ignored);
    // 不持有 socket，无需 close（socket 由 UdpListener 管理）

    // 优雅关闭 SSL（发送 close_notify，非阻塞）
    if (ssl_) {
        SSL_shutdown(ssl_);
    }

    BEAST_LOG_INFO("DtlsTransport closed: {}", reason);
    if (on_closed_) {
        on_closed_(reason);
    }
}

} // namespace beast::platform::net::transport
