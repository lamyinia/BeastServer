package main

import (
	"context"
	"fmt"
	"sync"
	"time"

	"global-workbench/internal/roomctl"
)

// RoomctlService 暴露 beastserver RoomService gRPC 客户端能力给前端。
// 前端通过 wails3 bindings 调用 SetAddr / CreateRoom。
//
// 设计要点：
//   - 默认 addr=""，前端必须先调 SetAddr 再 CreateRoom
//   - SetAddr 重置已有 client（关闭旧 gRPC 连接），下次 CreateRoom 时懒创建
//   - 所有日志走 logger.For(TagRoomctl)，前端 roomctl tab 可见
//   - CreateRoom 内部用 30s 超时（gRPC 真实场景下 beastserver 创建房间很快，留余量）
type RoomctlService struct {
	mu     sync.Mutex
	addr   string
	client *roomctl.Client
}

// NewRoomctlService 创建默认配置的 service。
func NewRoomctlService() *RoomctlService {
	return &RoomctlService{}
}

// SetAddr 设置 beastserver gRPC 地址（形如 "192.168.217.130:9010"）。
// 会重置已有 client（关闭旧 gRPC 连接），下次 CreateRoom 时重新 New。
func (s *RoomctlService) SetAddr(addr string) error {
	s.mu.Lock()
	defer s.mu.Unlock()

	if s.client != nil {
		_ = s.client.Close()
		s.client = nil
	}
	s.addr = addr
	return nil
}

// ensureClientLocked 调用方持锁。懒创建 client。
func (s *RoomctlService) ensureClientLocked() error {
	if s.addr == "" {
		return fmt.Errorf("roomctl: addr not set, call SetAddr first")
	}
	if s.client == nil {
		c, err := roomctl.New(s.addr)
		if err != nil {
			return err
		}
		s.client = c
	}
	return nil
}

// CreateRoom 调用 RoomService.CreateRoom。
// 返回 RoomCreatedResult（前端按 camelCase 字段 instanceID / engineName 取值）。
//
// engineName：玩法引擎名（如 "pixelmoba"）
// playerIDs：额外玩家 id 列表（v1 留空即可，发起者 Session 由平台自动 bind）
// instanceID：指定 instance_id（测试/幂等）；为空由平台生成
func (s *RoomctlService) CreateRoom(engineName string, playerIDs []string, instanceID string) (*RoomCreatedResult, error) {
	s.mu.Lock()
	defer s.mu.Unlock()

	if err := s.ensureClientLocked(); err != nil {
		return nil, err
	}

	// 上层 RPC 超时 30s（roomctl.Client 内部还会加 10s 兜底）
	ctx, cancel := context.WithTimeout(context.Background(), 30*time.Second)
	defer cancel()

	res, err := s.client.CreateRoom(ctx, engineName, playerIDs, instanceID)
	if err != nil {
		// gRPC 失败时重置 client，避免连接坏掉后续都用旧 conn
		_ = s.client.Close()
		s.client = nil
		return nil, err
	}
	return &RoomCreatedResult{
		InstanceID: res.InstanceID,
		EngineName: res.EngineName,
	}, nil
}

// RoomCreatedResult CreateRoom 的返回值。
// json tag 走 camelCase 让前端字段名风格一致。
type RoomCreatedResult struct {
	InstanceID string `json:"instanceID"`
	EngineName string `json:"engineName"`
}

// Close 释放 gRPC 连接。应用退出时调用。
func (s *RoomctlService) Close() error {
	s.mu.Lock()
	defer s.mu.Unlock()
	if s.client != nil {
		err := s.client.Close()
		s.client = nil
		return err
	}
	return nil
}
