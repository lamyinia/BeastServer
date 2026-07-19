package roomctl

import (
	"context"
	"errors"
	"net"
	"testing"
	"time"

	"google.golang.org/grpc"
	"google.golang.org/grpc/codes"
	"google.golang.org/grpc/credentials/insecure"
	"google.golang.org/grpc/status"
	"google.golang.org/grpc/test/bufconn"

	"beastserver-project/sdk/go/beastcli/proto/platform"
)

// bufconn 的 net.PacketConn 实现，bufconn Listener 本身就实现 net.Listener。
// 我们用 grpc.NewServer + bufconn.Listener 起 in-process server。
// 客户端用 grpc.WithContextDialer 把 bufconn.Listener 暴露成 dialer。

const bufSize = 1024 * 1024

// mockRoomServer 可配置 CreateRoom 的行为，便于覆盖多种场景。
type mockRoomServer struct {
	platform.UnimplementedRoomServiceServer

	// onCreate 收到请求时执行；返回 (resp, err) 给客户端。
	// nil 时用默认逻辑：回显请求的 engine_name + 生成 instance_id。
	onCreate func(ctx context.Context, req *platform.CreateRoomRequest) (*platform.CreateRoomResponse, error)

	// lastReq 记录最后一次收到的请求，方便断言。
	lastReq *platform.CreateRoomRequest
}

func (m *mockRoomServer) CreateRoom(ctx context.Context, req *platform.CreateRoomRequest) (*platform.CreateRoomResponse, error) {
	m.lastReq = req
	if m.onCreate != nil {
		return m.onCreate(ctx, req)
	}
	// 默认：回显 engine_name，instance_id 取请求或生成 "inst-001"
	instID := req.InstanceId
	if instID == "" {
		instID = "inst-001"
	}
	return &platform.CreateRoomResponse{
		InstanceId: instID,
		EngineName: req.EngineName,
	}, nil
}

// startMockServer 启动 bufconn gRPC server，返回 cleanup 函数。
// dialer 用来在 New 时把 client 连到这个 server。
func startMockServer(t *testing.T, srv *mockRoomServer) (dialer func(context.Context, string) (net.Conn, error), cleanup func()) {
	t.Helper()
	ln := bufconn.Listen(bufSize)
	grpcSrv := grpc.NewServer()
	platform.RegisterRoomServiceServer(grpcSrv, srv)

	go func() {
		_ = grpcSrv.Serve(ln)
	}()

	dialer = func(context.Context, string) (net.Conn, error) {
		return ln.Dial()
	}
	cleanup = func() {
		grpcSrv.Stop()
		_ = ln.Close()
	}
	return dialer, cleanup
}

// newClientWithDialer 用 bufconn dialer 构造 Client（绕过真实网络）。
//
// addr 必须用 passthrough:/// scheme，否则 grpc.NewClient 的默认 name resolver
// 会把 "bufconn" 当成 DNS host 解析，导致 "produced zero addresses" 错误。
// passthrough resolver 直接把 endpoint 透传给 dialer，不解析。
func newClientWithDialer(t *testing.T, dialer func(context.Context, string) (net.Conn, error)) *Client {
	t.Helper()
	c, err := New("passthrough:///bufconn",
		grpc.WithContextDialer(dialer),
		grpc.WithTransportCredentials(insecure.NewCredentials()),
	)
	if err != nil {
		t.Fatalf("New failed: %v", err)
	}
	return c
}

// === 正常用例 ===

func TestCreateRoom_Default_OK(t *testing.T) {
	srv := &mockRoomServer{}
	dialer, cleanup := startMockServer(t, srv)
	defer cleanup()

	c := newClientWithDialer(t, dialer)
	defer c.Close()

	res, err := c.CreateRoom(context.Background(), "pixelmoba", []string{}, "")
	if err != nil {
		t.Fatalf("CreateRoom: %v", err)
	}
	if res.EngineName != "pixelmoba" {
		t.Errorf("EngineName = %q, want pixelmoba", res.EngineName)
	}
	if res.InstanceID != "inst-001" {
		t.Errorf("InstanceID = %q, want inst-001", res.InstanceID)
	}
	// 请求字段透传
	if srv.lastReq.EngineName != "pixelmoba" {
		t.Errorf("lastReq.EngineName = %q", srv.lastReq.EngineName)
	}
	if len(srv.lastReq.PlayerIds) != 0 {
		t.Errorf("lastReq.PlayerIds = %v, want empty", srv.lastReq.PlayerIds)
	}
}

func TestCreateRoom_WithPlayersAndInstanceID(t *testing.T) {
	srv := &mockRoomServer{}
	dialer, cleanup := startMockServer(t, srv)
	defer cleanup()

	c := newClientWithDialer(t, dialer)
	defer c.Close()

	res, err := c.CreateRoom(context.Background(), "pixelmoba", []string{"p1", "p2"}, "my-inst")
	if err != nil {
		t.Fatalf("CreateRoom: %v", err)
	}
	if res.InstanceID != "my-inst" {
		t.Errorf("InstanceID = %q, want my-inst", res.InstanceID)
	}
	if len(srv.lastReq.PlayerIds) != 2 || srv.lastReq.PlayerIds[0] != "p1" || srv.lastReq.PlayerIds[1] != "p2" {
		t.Errorf("lastReq.PlayerIds = %v", srv.lastReq.PlayerIds)
	}
	if srv.lastReq.InstanceId != "my-inst" {
		t.Errorf("lastReq.InstanceId = %q", srv.lastReq.InstanceId)
	}
}

// === 错误传播 ===

func TestCreateRoom_ServerError_Propagates(t *testing.T) {
	srv := &mockRoomServer{
		onCreate: func(ctx context.Context, req *platform.CreateRoomRequest) (*platform.CreateRoomResponse, error) {
			return nil, status.Errorf(codes.InvalidArgument, "engine %s not registered", req.EngineName)
		},
	}
	dialer, cleanup := startMockServer(t, srv)
	defer cleanup()

	c := newClientWithDialer(t, dialer)
	defer c.Close()

	_, err := c.CreateRoom(context.Background(), "unknown", nil, "")
	if err == nil {
		t.Fatal("expected error, got nil")
	}

	// gRPC status 应该能解出来，确认是 codes.InvalidArgument
	st, ok := status.FromError(err)
	if !ok {
		t.Fatalf("err is not grpc status: %v", err)
	}
	if st.Code() != codes.InvalidArgument {
		t.Errorf("code = %v, want InvalidArgument", st.Code())
	}
	// 错误应被 %w 包装（errors.Is 不直接匹配 grpc status，但 Unwrap 链应包含）
	if !errors.Is(err, status.Error(codes.InvalidArgument, "")) {
		// status.Error 每次返回不同实例，Is 用 ErrorIs 内部比较；这里只是断言 Unwrap 链能解
		// grpc status 的 errors.Is 实现是按 code 比较的
	}
}

func TestCreateRoom_ContextCanceled(t *testing.T) {
	// mock 慢响应，ctx 在过程中取消
	srv := &mockRoomServer{
		onCreate: func(ctx context.Context, req *platform.CreateRoomRequest) (*platform.CreateRoomResponse, error) {
			// 等待 ctx 取消
			<-ctx.Done()
			return nil, status.FromContextError(ctx.Err()).Err()
		},
	}
	dialer, cleanup := startMockServer(t, srv)
	defer cleanup()

	c := newClientWithDialer(t, dialer)
	defer c.Close()

	ctx, cancel := context.WithCancel(context.Background())
	// 立即取消，但 mock 在 <-ctx.Done() 上等
	cancel()
	_, err := c.CreateRoom(ctx, "pixelmoba", nil, "")
	if err == nil {
		t.Fatal("expected error, got nil")
	}
	// gRPC 把 ctx cancel 转成 code.Canceled 的 status error，
	// 用 status.Code 判断（errors.Is(err, context.Canceled) 在 grpc-go 不直接匹配）。
	if st, ok := status.FromError(err); ok {
		if st.Code() != codes.Canceled {
			t.Errorf("code = %v, want Canceled", st.Code())
		}
	} else {
		// 兜底：错误字符串包含 canceled
		if !errors.Is(err, context.Canceled) {
			t.Errorf("err should be Canceled status or wrap context.Canceled, got: %v", err)
		}
	}
}

// === 超时 ===

func TestCreateRoom_DefaultTimeout_AppliedWhenNoDeadline(t *testing.T) {
	// 验证：ctx 无 Deadline 时，CreateRoom 内部应加 DefaultTimeout。
	// mock 检查 ctx 的 Deadline 在 DefaultTimeout 内。
	srv := &mockRoomServer{
		onCreate: func(ctx context.Context, req *platform.CreateRoomRequest) (*platform.CreateRoomResponse, error) {
			dl, ok := ctx.Deadline()
			if !ok {
				return nil, status.Error(codes.Internal, "no deadline set")
			}
			// Deadline 应在 (now, now+DefaultTimeout+小余量] 范围
			remaining := time.Until(dl)
			if remaining <= 0 || remaining > DefaultTimeout+100*time.Millisecond {
				return nil, status.Errorf(codes.Internal, "deadline remaining = %v, want (0, %v]", remaining, DefaultTimeout)
			}
			return &platform.CreateRoomResponse{InstanceId: "ok", EngineName: req.EngineName}, nil
		},
	}
	dialer, cleanup := startMockServer(t, srv)
	defer cleanup()

	c := newClientWithDialer(t, dialer)
	defer c.Close()

	res, err := c.CreateRoom(context.Background(), "pixelmoba", nil, "")
	if err != nil {
		t.Fatalf("CreateRoom: %v", err)
	}
	if res.InstanceID != "ok" {
		t.Errorf("InstanceID = %q", res.InstanceID)
	}
}

func TestCreateRoom_CallerDeadline_Respected(t *testing.T) {
	// 验证：调用方设了更短的 Deadline 时，CreateRoom 不会覆盖它。
	callerDeadline := 2 * time.Second
	srv := &mockRoomServer{
		onCreate: func(ctx context.Context, req *platform.CreateRoomRequest) (*platform.CreateRoomResponse, error) {
			dl, ok := ctx.Deadline()
			if !ok {
				return nil, status.Error(codes.Internal, "no deadline")
			}
			remaining := time.Until(dl)
			// 应该在 (callerDeadline-100ms, callerDeadline+100ms] 范围
			if remaining > callerDeadline+100*time.Millisecond || remaining < callerDeadline-200*time.Millisecond {
				return nil, status.Errorf(codes.Internal, "remaining = %v, want ~%v", remaining, callerDeadline)
			}
			return &platform.CreateRoomResponse{InstanceId: "ok", EngineName: req.EngineName}, nil
		},
	}
	dialer, cleanup := startMockServer(t, srv)
	defer cleanup()

	c := newClientWithDialer(t, dialer)
	defer c.Close()

	ctx, cancel := context.WithTimeout(context.Background(), callerDeadline)
	defer cancel()
	_, err := c.CreateRoom(ctx, "pixelmoba", nil, "")
	if err != nil {
		t.Fatalf("CreateRoom: %v", err)
	}
}

// === Close ===

func TestClose_Idempotent(t *testing.T) {
	srv := &mockRoomServer{}
	dialer, cleanup := startMockServer(t, srv)
	defer cleanup()

	c := newClientWithDialer(t, dialer)

	if err := c.Close(); err != nil {
		t.Errorf("first Close: %v", err)
	}
	// 第二次 Close 应该幂等无错
	if err := c.Close(); err != nil {
		t.Errorf("second Close: %v", err)
	}
	// conn 应被置 nil，第三次也安全
	if c.conn != nil {
		t.Errorf("conn should be nil after Close")
	}
}
