// Package roomctl 实现 beastserver RoomService 的 gRPC 客户端。
//
// 用途：工作台"建房"按钮触发，调 beastserver 9010 端口的
// beast.platform.RoomService/CreateRoom，得到 instance_id 用于后续 TCP 连接。
//
// 设计要点：
//   - 默认 insecure（v1 内网调试场景）；要 TLS 走 WithDialOption 注入
//   - CreateRoom 默认 10s 超时，ctx 已带 Deadline 时尊重调用方
//   - 所有操作推 logger.For(TagRoomctl)，前端"roomctl" tab 可见
//   - 错误用 fmt.Errorf 包一层 %w，方便调用方 errors.Is/As
package roomctl

import (
	"context"
	"fmt"
	"time"

	"google.golang.org/grpc"
	"google.golang.org/grpc/credentials/insecure"

	"global-workbench/internal/logger"
	"beastserver-project/sdk/go/beastcli/proto/platform"
)

// DefaultTimeout CreateRoom 默认 RPC 超时。
// 10s 足够覆盖 VM 上 beastserver 启动一个房间实例的耗时。
const DefaultTimeout = 10 * time.Second

// Client 是 RoomService 的 gRPC 客户端。
//
// 用法：
//
//	c, err := roomctl.New("192.168.217.130:9010")
//	if err != nil { ... }
//	defer c.Close()
//	res, err := c.CreateRoom(ctx, "pixelmoba", []string{"player1"}, "")
type Client struct {
	conn *grpc.ClientConn
	room platform.RoomServiceClient
	log  *logger.Logger
}

// New 连到 addr（形如 "host:9010"），返回 *Client。
//
// dialOpts 为空时默认 insecure；如需 TLS：
//
//	c, _ := roomctl.New(addr, grpc.WithTransportCredentials(credentials.NewTLS(&tls.Config{...})))
//
// 透传给 grpc.NewClient（gRPC 1.82+ 推荐的非阻塞连接方式）。
func New(addr string, dialOpts ...grpc.DialOption) (*Client, error) {
	if len(dialOpts) == 0 {
		dialOpts = []grpc.DialOption{
			grpc.WithTransportCredentials(insecure.NewCredentials()),
		}
	}
	conn, err := grpc.NewClient(addr, dialOpts...)
	if err != nil {
		return nil, fmt.Errorf("roomctl: grpc dial %s: %w", addr, err)
	}
	log := logger.For(logger.TagRoomctl)
	log.Info("grpc connected", map[string]any{"addr": addr})
	return &Client{
		conn: conn,
		room: platform.NewRoomServiceClient(conn),
		log:  log,
	}, nil
}

// CreateRoomResult 是 CreateRoom 的返回值，解耦 gRPC pb 类型。
// 调用方不需要 import platform 包。
type CreateRoomResult struct {
	InstanceID string
	EngineName string
}

// CreateRoom 调 RoomService.CreateRoom。
//
//   - engineName：玩法引擎名（如 "pixelmoba"），须已在服务端 register_engine 注册
//   - playerIDs：除发起者外额外加入房间的玩家（发起者 Session 由平台自动 bind，v1 留空即可）
//   - instanceID：指定 instance_id（测试/幂等）；为空由平台生成
//
// ctx 已带 Deadline 时尊重调用方；否则加 DefaultTimeout。
func (c *Client) CreateRoom(
	ctx context.Context,
	engineName string,
	playerIDs []string,
	instanceID string,
) (*CreateRoomResult, error) {
	if _, ok := ctx.Deadline(); !ok {
		var cancel context.CancelFunc
		ctx, cancel = context.WithTimeout(ctx, DefaultTimeout)
		defer cancel()
	}

	req := &platform.CreateRoomRequest{
		EngineName: engineName,
		PlayerIds:  playerIDs,
		InstanceId: instanceID,
	}
	c.log.Debug("CreateRoom", map[string]any{
		"engine":      engineName,
		"players":     len(playerIDs),
		"instance_id": instanceID,
	})

	resp, err := c.room.CreateRoom(ctx, req)
	if err != nil {
		c.log.Error("CreateRoom rpc failed", map[string]any{"err": err.Error()})
		return nil, fmt.Errorf("roomctl: CreateRoom: %w", err)
	}

	result := &CreateRoomResult{
		InstanceID: resp.InstanceId,
		EngineName: resp.EngineName,
	}
	c.log.Info("CreateRoom ok", map[string]any{
		"instance_id": result.InstanceID,
		"engine":      result.EngineName,
	})
	return result, nil
}

// Close 释放 gRPC 连接。多次调用幂等。
func (c *Client) Close() error {
	if c.conn == nil {
		return nil
	}
	err := c.conn.Close()
	c.conn = nil
	c.log.Info("grpc closed")
	return err
}
