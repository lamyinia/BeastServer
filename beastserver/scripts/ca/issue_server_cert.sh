#!/usr/bin/env bash
# issue_server_cert.sh — 用内部 CA 签发服务端 TLS 证书。
#
# 用途：为 BeastServer 的 TCP TLS 监听端口签发服务端证书。
#   - 生成的 server_cert.pem / server_key.pem 填入 server.json 的
#     net.tcp.tls.cert_path / net.tcp.tls.key_path。
#   - 签发后可用 `kill -HUP <pid>` 触发热重载（见 main.cpp SIGHUP 处理）。
#
# 用法：
#   ./scripts/ca/issue_server_cert.sh localhost            # CN=localhost，SAN=DNS:localhost,IP:127.0.0.1
#   ./scripts/ca/issue_server_cert.sh game.example.com 10.0.0.5
#   OUT_DIR=/path/to/certs ./scripts/ca/issue_server_cert.sh myhost
#   CERT_DAYS=825 ./scripts/ca/issue_server_cert.sh myhost
#
# 参数：
#   $1 — 服务端主机名/CN（必填）
#   $2 — 额外 IP 地址（可选，追加到 SAN）
#
# 依赖：需先运行 init_ca.sh 生成 CA。
#
# 产物（默认输出到 config/certs/）：
#   server_key.pem    — 服务端私钥（PEM）
#   server_cert.pem   — 服务端证书（PEM，含 SAN，由 CA 签名）
#   server_chain.pem  — 证书链（server_cert + ca_cert，部分客户端需要）
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

CA_DIR="${CA_DIR:-$REPO_ROOT/config/certs/ca}"
OUT_DIR="${OUT_DIR:-$REPO_ROOT/config/certs}"
CERT_DAYS="${CERT_DAYS:-825}"  # 默认约 2 年多（浏览器对 >825 天的证书会报错）
SERVER_KEY_ALG="${SERVER_KEY_ALG:-rsa}"  # rsa | ec

if [[ $# -lt 1 ]]; then
    echo "用法: $0 <hostname> [extra_ip]" >&2
    echo "  例: $0 localhost" >&2
    echo "      $0 game.example.com 10.0.0.5" >&2
    exit 1
fi

HOSTNAME="$1"
EXTRA_IP="${2:-}"

CA_KEY="$CA_DIR/ca_key.pem"
CA_CERT="$CA_DIR/ca_cert.pem"

if [[ ! -f "$CA_KEY" || ! -f "$CA_CERT" ]]; then
    echo "ERROR: CA 私钥/证书不存在于 $CA_DIR" >&2
    echo "       请先运行 ./scripts/ca/init_ca.sh" >&2
    exit 1
fi

mkdir -p "$OUT_DIR"
cd "$OUT_DIR"

SERVER_KEY="server_key.pem"
SERVER_CSR="server_csr.pem"
SERVER_CERT="server_cert.pem"
SERVER_CHAIN="server_chain.pem"

# 构建 SAN（Subject Alternative Name）：始终包含 DNS:hostname + IP:127.0.0.1，
# 追加用户指定的 extra_ip。SAN 是现代 TLS 校验的关键字段，CN 已被弃用。
SAN="DNS:${HOSTNAME},IP:127.0.0.1"
if [[ -n "$EXTRA_IP" ]]; then
    SAN="${SAN},IP:${EXTRA_IP}"
fi

# 生成服务端私钥。
case "$SERVER_KEY_ALG" in
    rsa)
        echo "生成 RSA 2048 服务端私钥..."
        openssl genrsa -out "$SERVER_KEY" 2048
        ;;
    ec)
        echo "生成 ECDSA P-256 服务端私钥..."
        openssl ecparam -name prime256v1 -genkey -noout -out "$SERVER_KEY"
        ;;
    *)
        echo "ERROR: 不支持的 SERVER_KEY_ALG=$SERVER_KEY_ALG（可选 rsa|ec）" >&2
        exit 1
        ;;
esac
chmod 600 "$SERVER_KEY"

# 生成 CSR（Certificate Signing Request）。
echo "生成 CSR（CN=${HOSTNAME}, SAN=${SAN}）..."
openssl req -new -key "$SERVER_KEY" -out "$SERVER_CSR" \
    -subj "/CN=${HOSTNAME}/O=BeastServer" \
    -addext "subjectAltName=${SAN}"

# 用 CA 签发服务端证书：basicConstraints=CA:FALSE；keyUsage 含 digitalSignature/keyEncipherment；
# extendedKeyUsage=serverAuth；subjectAltName 由 CSR 继承。
# -copy_extensions copy 让签发证书继承 CSR 中的 SAN（OpenSSL 1.1.1+ 支持）。
echo "用 CA 签发服务端证书（有效期 ${CERT_DAYS} 天）..."
openssl x509 -req -in "$SERVER_CSR" \
    -CA "$CA_CERT" -CAkey "$CA_KEY" -CAcreateserial \
    -out "$SERVER_CERT" -days "$CERT_DAYS" \
    -copy_extensions copy \
    -extfile <(cat <<EOF
basicConstraints=critical,CA:FALSE
keyUsage=critical,digitalSignature,keyEncipherment
extendedKeyUsage=serverAuth
subjectAltName=${SAN}
EOF
)

# 拼接证书链（server_cert + ca_cert），部分客户端需要完整链才能校验。
cat "$SERVER_CERT" "$CA_CERT" > "$SERVER_CHAIN"

# CSR 可删除（已签发完成，不再需要）。
rm -f "$SERVER_CSR"

echo ""
echo "服务端证书签发完成："
echo "  私钥:     $OUT_DIR/$SERVER_KEY"
echo "  证书:     $OUT_DIR/$SERVER_CERT"
echo "  证书链:   $OUT_DIR/$SERVER_CHAIN"
echo ""
echo "server.json 配置（net.tcp.tls）："
echo '  "tls": {'
echo '    "enabled": true,'
echo "    \"cert_path\": \"$OUT_DIR/$SERVER_CERT\","
echo "    \"key_path\":  \"$OUT_DIR/$SERVER_KEY\","
echo '    "min_version": "TLSv1.2",'
echo '    "cipher_list": ""'
echo '  }'
echo ""
echo "客户端需配置 CA 证书作为信任锚点：$CA_CERT"
echo "如服务端已运行，执行 kill -HUP <pid> 热重载证书（无需重启）。"
