package beastclient

import (
	"errors"
	"fmt"
	"time"

	"google.golang.org/protobuf/proto"

	"beastserver-project/sdk/go/beastcli/internal/transport"
)

// === 通用 SendBiz（DTO 化请求/响应 + functional options）===
//
// 取代 v0 时代 5 个手写 protowire 的 Send helper。调用方构造 DTO →
// SendBiz(route, reqDTO, respRoute, respDTO, opts...)，opts 控制 clientSeq /
// timeout / transport。
//
// 优势：
//   - 类型安全：proto schema 改字段名/类型时编译报错，而不是运行时返回零值
//   - 零手写 wire：proto.Marshal/Unmarshal 替代 protowire.AppendTag/ConsumeTag
//   - 一份代码覆盖所有 c2s/s2c_resp 配对的玩法协议
//   - functional options：未来加字段不破坏调用方
//
// 与低层 Send(route, payload []byte, clientSeq int) 的区别：
//   - Send   发原始 Envelope payload bytes（Login 已编码 AuthRequest 后用）
//   - SendBiz DTO 编解码 + 同步等响应（玩法协议 SendEcho/SeqEcho 等用）
//
// 同步等响应模式：注册临时 handler → Send → select 等响应 channel（带超时）→ defer UnregisterHandler。
// 单线程串行调用安全；并发调用同 respRoute 会冲突（handler 后注册覆盖前注册）。
// 并发场景用 SeqEcho（带 client_seq 配对，用 WithClientSeq 传帧头）。

// SendOption SendBiz 的可选参数（functional options 模式）。
type SendOption func(*sendConfig)

type sendConfig struct {
	clientSeq int
	timeout   time.Duration
	transport transport.Type // v3+ 多 channel 用，v1/v2 必须为空
}

// WithClientSeq 指定帧头 client_seq（SeqEcho 等配对场景用）。
// 默认 0。
func WithClientSeq(seq int) SendOption {
	return func(c *sendConfig) { c.clientSeq = seq }
}

// WithTimeout 指定同步等响应超时；不调用则用 DefaultTimeout（10s）。
func WithTimeout(d time.Duration) SendOption {
	return func(c *sendConfig) { c.timeout = d }
}

// WithTransport 指定发送 transport 类型（v3+ 多 channel 调度用，如 TypeKCP）。
// v1/v2 单 transport 阶段不支持，传非空 SendBiz 会报错。
func WithTransport(t transport.Type) SendOption {
	return func(c *sendConfig) { c.transport = t }
}

// SendBiz 通用 DTO 化请求/响应：发 req 到 route，同步等 respRoute 的响应并
// unmarshal 到 resp。
//
// 参数：
//   - route:     请求路由（c2s），如 RouteEchoRequest
//   - req:       请求 DTO（proto.Message），proto.Marshal 编码后 Send
//   - respRoute: 响应路由（s2c_resp），如 RouteEchoResponse
//   - resp:      响应 DTO 指针（proto.Message），handler 里 proto.Unmarshal 写入
//   - opts:      可选参数（WithClientSeq / WithTimeout / WithTransport）
//
// 返回 nil 表示 resp 已填充。错误来源：marshal/send/unmarshal/timeout/closed/transport 不支持。
func (c *BeastClient) SendBiz(route string, req proto.Message, respRoute string, resp proto.Message, opts ...SendOption) error {
	cfg := &sendConfig{}
	for _, opt := range opts {
		opt(cfg)
	}
	if cfg.timeout <= 0 {
		cfg.timeout = DefaultTimeout
	}

	// v1/v2 单 transport 校验：WithTransport 传非空直接报错。
	// v3+ 实现 transports map 后取消此校验，按 cfg.transport 选 channel。
	if cfg.transport != "" {
		return fmt.Errorf("beastclient: WithTransport(%q) not supported in v1/v2 (single transport only); v3+ multi-channel required", cfg.transport)
	}

	payload, err := proto.Marshal(req)
	if err != nil {
		return fmt.Errorf("beastclient: marshal %T: %w", req, err)
	}

	// handler 在 readLoop goroutine 同步执行，禁止阻塞。
	// unmarshal 后用 non-blocking send 推结果（容量 1，重复响应丢弃）。
	errCh := make(chan error, 1)
	c.RegisterHandler(respRoute, func(p []byte, _ int) {
		if err := proto.Unmarshal(p, resp); err != nil {
			select {
			case errCh <- fmt.Errorf("beastclient: unmarshal %T: %w", resp, err):
			default:
			}
			return
		}
		select {
		case errCh <- nil:
		default:
		}
	})
	defer c.UnregisterHandler(respRoute)

	if err := c.Send(route, payload, cfg.clientSeq); err != nil {
		return fmt.Errorf("beastclient: send %T: %w", req, err)
	}

	select {
	case err := <-errCh:
		return err
	case <-time.After(cfg.timeout):
		return fmt.Errorf("beastclient: %s timeout", route)
	case <-c.closed:
		return errors.New("beastclient: closed during request")
	}
}

// === BigEcho payload 校验工具 ===

// VerifyBigEchoPayload 校验 BigEcho 回包 payload 是否符合 byte[i] = i & 0xFF 模式。
// 返回 nil 表示校验通过，否则返回第一个不匹配的索引 + 期望值 + 实际值。
//
// 用法：bc.SendBiz(RouteBigEchoRequest, &sdk_event.BigEchoRequest{Size: n},
//     RouteBigEchoResponse, &resp, WithTimeout(timeout)); VerifyBigEchoPayload(resp.Payload)
func VerifyBigEchoPayload(payload []byte) error {
	for i, b := range payload {
		want := byte(i & 0xFF)
		if b != want {
			return fmt.Errorf("beastclient: big echo payload mismatch at index %d: want=0x%02x got=0x%02x", i, want, b)
		}
	}
	return nil
}
