#!/usr/bin/env bash
# init_ca.sh — 初始化内部自建 CA（生成 CA 私钥 + 自签名 CA 证书）。
#
# 用途：BeastServer TLS/KCP 加密链路的根信任锚点。
#   - 服务端证书由 issue_server_cert.sh 用本 CA 签发。
#   - 客户端配置 trust_anchor 为 ca_cert.pem（非 mTLS 场景仅校验服务端证书链）。
#
# 用法：
#   ./scripts/ca/init_ca.sh                          # 默认输出到 config/certs/ca/
#   CA_DIR=/path/to/ca ./scripts/ca/init_ca.sh       # 自定义输出目录
#   CA_DAYS=3650 ./scripts/ca/init_ca.sh             # 自定义 CA 有效期（默认 10 年）
#   CA_KEY_ALG=ec ./scripts/ca/init_ca.sh            # ec=ECDSA P-256（默认 rsa=RSA 4096）
#
# 产物：
#   $CA_DIR/ca_key.pem   — CA 私钥（PEM，无密码保护；务必妥善保管，勿泄露）
#   $CA_DIR/ca_cert.pem  — 自签名 CA 证书（PEM，X.509 v3 CA 扩展）
#
set -euo pipefail

# 脚本所在目录（用于推导默认 CA_DIR）。
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# beastserver 仓库根（scripts/ca/ 的上两级）。
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

CA_DIR="${CA_DIR:-$REPO_ROOT/config/certs/ca}"
CA_DAYS="${CA_DAYS:-3650}"
CA_KEY_ALG="${CA_KEY_ALG:-rsa}"  # rsa | ec

mkdir -p "$CA_DIR"
cd "$CA_DIR"

CA_KEY="ca_key.pem"
CA_CERT="ca_cert.pem"

# 已存在则报错，避免误覆盖正在使用的 CA（会导致所有已签发证书失效）。
if [[ -f "$CA_KEY" || -f "$CA_CERT" ]]; then
    echo "ERROR: $CA_KEY 或 $CA_CERT 已存在于 $CA_DIR" >&2
    echo "       如需重建 CA，请先删除旧文件（注意：所有已签发的服务端证书将失效）" >&2
    exit 1
fi

# 生成 CA 私钥。
# RSA 4096 提供足够的安全余量；ECDSA P-256 更快、密钥更短。
case "$CA_KEY_ALG" in
    rsa)
        echo "生成 RSA 4096 CA 私钥..."
        openssl genrsa -out "$CA_KEY" 4096
        ;;
    ec)
        echo "生成 ECDSA P-256 CA 私钥..."
        openssl ecparam -name prime256v1 -genkey -noout -out "$CA_KEY"
        ;;
    *)
        echo "ERROR: 不支持的 CA_KEY_ALG=$CA_KEY_ALG（可选 rsa|ec）" >&2
        exit 1
        ;;
esac
chmod 600 "$CA_KEY"

# 自签名 CA 证书：basicConstraints=critical,CA:TRUE；keyUsage 含 keyCertSign/cRLSign。
# -addext 在 openssl req 中直接附加 v3 扩展（OpenSSL 1.1.1+ 支持）。
echo "自签名 CA 证书（有效期 ${CA_DAYS} 天）..."
openssl req -new -x509 -key "$CA_KEY" -out "$CA_CERT" -days "$CA_DAYS" \
    -subj "/CN=BeastServer Internal CA/O=BeastServer" \
    -addext "basicConstraints=critical,CA:TRUE" \
    -addext "keyUsage=critical,keyCertSign,cRLSign" \
    -addext "subjectKeyIdentifier=hash"

echo ""
echo "CA 初始化完成："
echo "  CA 私钥: $CA_DIR/$CA_KEY"
echo "  CA 证书: $CA_DIR/$CA_CERT"
echo ""
echo "下一步：用 issue_server_cert.sh 签发服务端证书。"
echo "  客户端应将 ca_cert.pem 作为信任锚点（trust anchor）。"
