package main

import (
	"context"
	"encoding/hex"
	"fmt"
	"sync"
	"time"

	"global-workbench/internal/logger"

	"beastserver-project/sdk/go/beastcli"
	"beastserver-project/sdk/go/beastcli/proto/biz/sdk_event"
)

// ConnectParams CreateLink 的参数（结构体形式，便于 Wails 序列化）。
// 替代 functional options（Wails 不支持 function type）。
type ConnectParams struct {
	Host      string     `json:"host"`
	Port      int        `json:"port"`
	Transport string     `json:"transport"` // "tcp" / "tls" / "kcp" / "websocket"；空默认 tcp
	TLS       *TLSParams `json:"tls"`       // 仅 transport=tls 时生效；其他 transport 忽略
}

// TLSParams TLS 连接参数（前端表单字段直传）。
type TLSParams struct {
	CAPath     string `json:"caPath"`     // CA 证书路径（PEM 格式，必填）
	ServerName string `json:"serverName"` // SNI 主机名，必须匹配证书 SAN；空时用 Host
	CertPath   string `json:"certPath"`   // mTLS 客户端证书路径（可选，当前服务端未启用 verify_client）
	KeyPath    string `json:"keyPath"`    // mTLS 客户端私钥路径（可选，与 CertPath 配对）
}

// LinkInfo 单条链路的对外状态（ListLinks 返回）。
// 前端轮询 ListLinks 拿到这个数组渲染左侧链路列表。
type LinkInfo struct {
	ID        string `json:"id"`
	Host      string `json:"host"`
	Port      int    `json:"port"`
	Transport string `json:"transport"` // tcp / tls
	State     string `json:"state"`     // DISCONNECTED / CONNECTING / CONNECTED / AUTHING / AUTHED
}

// sdkLink 单条链路的内部上下文。
// 持一个独立的 BeastClient，可与其他链路并行收发。
type sdkLink struct {
	bc        *beastcli.BeastClient
	host      string
	port      int
	transport string
}

// SdkEventService 暴露 sdk_event 玩法 5 条 route 的多链路联调能力给前端。
//
// 设计要点（2026-07-20 多链路重构）：
//   - links map[linkID]*sdkLink：每个 linkID 对应一条独立 BeastClient
//   - 全部 API 首参 linkID，无"活动链路"隐式状态
//   - SendBiz 不持 s.mu 等响应（避免阻塞其他链路），仅 requireAuthedLink 时持锁读 link
//   - 关闭某条链路不影响其他链路
//   - 前端轮询 ListLinks 刷新状态灯
//
// 与 beastcli 包对齐的 5 条 route：
//  1. sdk.echo        → sdk.echo.resp         (string echo)
//  2. sdk.echo.seq    → sdk.echo.seq.resp     (client_seq 透传)
//  3. sdk.echo.bytes  → sdk.echo.bytes.resp   (bytes 二进制透传)
//  4. sdk.echo.big    → sdk.echo.big.resp     (大消息，byte[i] = i & 0xFF)
//  5. sdk.transport.info → sdk.transport.info.resp (transport 调度)
type SdkEventService struct {
	mu    sync.Mutex
	links map[string]*sdkLink
	log   *logger.Logger
}

// NewSdkEventService 创建默认配置的 service。
func NewSdkEventService() *SdkEventService {
	return &SdkEventService{
		links: make(map[string]*sdkLink),
		log:   logger.For(logger.TagTargetGo),
	}
}

// === 链路管理 ===

// CreateLink 创建并连接一条新链路。
// linkID 必须不重复；重复返回错误（需先 CloseLink）。
//
// params.Transport 控制传输层选择：
//   - "tcp"（默认） / "tls"：当前可用
//   - "kcp" / "websocket"：beastcli v3/v4 才实现，调用会返回错误
//
// params.TLS 仅在 Transport="tls" 时生效；transport=tls 时 TLS.CAPath 必填。
func (s *SdkEventService) CreateLink(linkID string, params ConnectParams) error {
	if linkID == "" {
		return fmt.Errorf("sdk_event: linkID empty")
	}
	s.mu.Lock()
	defer s.mu.Unlock()

	if _, exists := s.links[linkID]; exists {
		return fmt.Errorf("sdk_event: link %q already exists, close it first", linkID)
	}

	bc, transportType, err := s.buildAndConnect(params)
	if err != nil {
		return err
	}

	s.links[linkID] = &sdkLink{
		bc:        bc,
		host:      params.Host,
		port:      params.Port,
		transport: string(transportType),
	}
	s.log.Info("link created", map[string]any{
		"link_id":   linkID,
		"host":      params.Host,
		"port":      params.Port,
		"transport": string(transportType),
	})
	return nil
}

// CloseLink 关闭指定链路。linkID 不存在返回错误。
func (s *SdkEventService) CloseLink(linkID string) error {
	s.mu.Lock()
	defer s.mu.Unlock()
	link, ok := s.links[linkID]
	if !ok {
		return fmt.Errorf("sdk_event: link %q not found", linkID)
	}
	link.bc.Close()
	delete(s.links, linkID)
	s.log.Info("link closed", map[string]any{"link_id": linkID})
	return nil
}

// CloseAll 关闭所有链路（应用退出时调）。
func (s *SdkEventService) CloseAll() error {
	s.mu.Lock()
	defer s.mu.Unlock()
	for id, link := range s.links {
		link.bc.Close()
		delete(s.links, id)
	}
	s.log.Info("all links closed")
	return nil
}

// LoginLink 在指定链路上 Login。
// 必须在该链路 CreateLink 之后调用。Login 成功后该链路 bc 进入 AUTHED 状态。
func (s *SdkEventService) LoginLink(linkID, token, deviceID, version string) error {
	bc, err := s.requireLink(linkID)
	if err != nil {
		return err
	}
	if err := bc.Login(token, deviceID, version); err != nil {
		return fmt.Errorf("sdk_event: login[%s]: %w", linkID, err)
	}
	s.log.Info("link login ok", map[string]any{
		"link_id":   linkID,
		"token":     token,
		"device_id": deviceID,
	})
	return nil
}

// StateLink 返回指定链路状态字符串。
// linkID 不存在返回 DISCONNECTED + error。
func (s *SdkEventService) StateLink(linkID string) (string, error) {
	s.mu.Lock()
	link, ok := s.links[linkID]
	s.mu.Unlock()
	if !ok {
		return beastcli.StateDisconnected.String(), fmt.Errorf("sdk_event: link %q not found", linkID)
	}
	return link.bc.State().String(), nil
}

// ListLinks 返回所有链路的当前状态。前端轮询这个刷新左侧链路列表的状态灯。
func (s *SdkEventService) ListLinks() ([]LinkInfo, error) {
	s.mu.Lock()
	defer s.mu.Unlock()
	out := make([]LinkInfo, 0, len(s.links))
	for id, link := range s.links {
		out = append(out, LinkInfo{
			ID:        id,
			Host:      link.host,
			Port:      link.port,
			Transport: link.transport,
			State:     link.bc.State().String(),
		})
	}
	return out, nil
}

// === 1) Echo (string) ===

// EchoResult SendEcho 的返回值。
type EchoResult struct {
	Text string `json:"text"`
}

// SendEcho 在 linkID 指定链路上发 string echo，同步等响应。
func (s *SdkEventService) SendEcho(linkID, text string, timeoutMs int) (*EchoResult, error) {
	bc, err := s.requireAuthedLink(linkID)
	if err != nil {
		return nil, err
	}
	resp := &sdk_event.EchoResponse{}
	err = bc.SendBiz(
		beastcli.RouteEchoRequest,
		&sdk_event.EchoRequest{Text: text},
		beastcli.RouteEchoResponse,
		resp,
		beastcli.WithTimeout(msToDuration(timeoutMs)),
	)
	if err != nil {
		return nil, err
	}
	return &EchoResult{Text: resp.GetText()}, nil
}

// === 2) SeqEcho (client_seq 透传) ===

// SeqEchoResultDTO SendSeqEcho 的返回值。
type SeqEchoResultDTO struct {
	ClientSeq uint64 `json:"clientSeq"`
	Text      string `json:"text"`
}

// SendSeqEcho 在 linkID 指定链路上发 SeqEchoRequest 带 clientSeq，同步等响应。
func (s *SdkEventService) SendSeqEcho(linkID, text string, clientSeq, timeoutMs int) (*SeqEchoResultDTO, error) {
	bc, err := s.requireAuthedLink(linkID)
	if err != nil {
		return nil, err
	}
	resp := &sdk_event.SeqEchoResponse{}
	err = bc.SendBiz(
		beastcli.RouteSeqEchoRequest,
		&sdk_event.SeqEchoRequest{Text: text},
		beastcli.RouteSeqEchoResponse,
		resp,
		beastcli.WithClientSeq(clientSeq),
		beastcli.WithTimeout(msToDuration(timeoutMs)),
	)
	if err != nil {
		return nil, err
	}
	return &SeqEchoResultDTO{
		ClientSeq: resp.GetClientSeq(),
		Text:      resp.GetText(),
	}, nil
}

// === 3) BytesEcho (bytes 二进制透传) ===

// BytesEchoResultDTO SendBytesEcho 的返回值。HexPayload 是 hex 编码的 bytes。
type BytesEchoResultDTO struct {
	HexPayload string `json:"hexPayload"`
	Len        int    `json:"len"`
}

// SendBytesEcho 在 linkID 指定链路上发 BytesEchoRequest 带 hexPayload（hex 字符串），同步等响应。
func (s *SdkEventService) SendBytesEcho(linkID, hexPayload string, timeoutMs int) (*BytesEchoResultDTO, error) {
	bc, err := s.requireAuthedLink(linkID)
	if err != nil {
		return nil, err
	}
	payload, err := hex.DecodeString(hexPayload)
	if err != nil {
		return nil, fmt.Errorf("sdk_event: decode hex payload: %w", err)
	}
	resp := &sdk_event.BytesEchoResponse{}
	err = bc.SendBiz(
		beastcli.RouteBytesEchoRequest,
		&sdk_event.BytesEchoRequest{Payload: payload},
		beastcli.RouteBytesEchoResponse,
		resp,
		beastcli.WithTimeout(msToDuration(timeoutMs)),
	)
	if err != nil {
		return nil, err
	}
	got := resp.GetPayload()
	return &BytesEchoResultDTO{
		HexPayload: hex.EncodeToString(got),
		Len:        len(got),
	}, nil
}

// === 4) BigEcho (大消息 echo) ===

// BigEchoResultDTO SendBigEcho 的返回值。
type BigEchoResultDTO struct {
	Size       uint32 `json:"size"`
	HexPreview string `json:"hexPreview"` // 前 32 字节的 hex 预览（避免 60KB 全量回传）
	Verified   bool   `json:"verified"`   // VerifyBigEchoPayload 是否通过
	VerifyErr  string `json:"verifyErr"`  // 校验失败时的错误（Verified=true 时为空）
}

// SendBigEcho 在 linkID 指定链路上发 BigEchoRequest 指定 size，同步等响应。
func (s *SdkEventService) SendBigEcho(linkID string, size uint32, timeoutMs int) (*BigEchoResultDTO, error) {
	bc, err := s.requireAuthedLink(linkID)
	if err != nil {
		return nil, err
	}
	resp := &sdk_event.BigEchoResponse{}
	err = bc.SendBiz(
		beastcli.RouteBigEchoRequest,
		&sdk_event.BigEchoRequest{Size: size},
		beastcli.RouteBigEchoResponse,
		resp,
		beastcli.WithTimeout(msToDuration(timeoutMs)),
	)
	if err != nil {
		return nil, err
	}
	payload := resp.GetPayload()

	verifyErr := beastcli.VerifyBigEchoPayload(payload)
	verified := verifyErr == nil
	verifyErrStr := ""
	if !verified {
		verifyErrStr = verifyErr.Error()
	}

	previewLen := 32
	if len(payload) < previewLen {
		previewLen = len(payload)
	}
	hexPreview := hex.EncodeToString(payload[:previewLen])

	return &BigEchoResultDTO{
		Size:       resp.GetSize(),
		HexPreview: hexPreview,
		Verified:   verified,
		VerifyErr:  verifyErrStr,
	}, nil
}

// === 5) TransportInfo (transport 调度测试) ===

// TransportInfoResultDTO SendTransportInfo 的返回值。
type TransportInfoResultDTO struct {
	RequestedPreference string `json:"requestedPreference"`
	PlayerID            string `json:"playerId"`
}

// SendTransportInfo 在 linkID 指定链路上发 TransportInfoRequest 带 preference，同步等响应。
func (s *SdkEventService) SendTransportInfo(linkID, preference string, timeoutMs int) (*TransportInfoResultDTO, error) {
	bc, err := s.requireAuthedLink(linkID)
	if err != nil {
		return nil, err
	}
	resp := &sdk_event.TransportInfoResponse{}
	err = bc.SendBiz(
		beastcli.RouteTransportInfoRequest,
		&sdk_event.TransportInfoRequest{Preference: preference},
		beastcli.RouteTransportInfoResponse,
		resp,
		beastcli.WithTimeout(msToDuration(timeoutMs)),
	)
	if err != nil {
		return nil, err
	}
	return &TransportInfoResultDTO{
		RequestedPreference: resp.GetRequestedPreference(),
		PlayerID:            resp.GetPlayerId(),
	}, nil
}

// === helpers ===

// requireLink 持锁拿 linkID 对应的 BeastClient 引用，不校验 state。
// 调用方拿到 bc 后即可释放 s.mu，避免 SendBiz 阻塞时卡住其他链路操作。
func (s *SdkEventService) requireLink(linkID string) (*beastcli.BeastClient, error) {
	s.mu.Lock()
	link, ok := s.links[linkID]
	s.mu.Unlock()
	if !ok {
		return nil, fmt.Errorf("sdk_event: link %q not found", linkID)
	}
	return link.bc, nil
}

// requireAuthedLink 拿 linkID 对应的 BeastClient 并校验已 AUTHED。
// 不持 s.mu 等响应（SendBiz 内部可能阻塞 10s）。
func (s *SdkEventService) requireAuthedLink(linkID string) (*beastcli.BeastClient, error) {
	bc, err := s.requireLink(linkID)
	if err != nil {
		return nil, err
	}
	state := bc.State()
	if state != beastcli.StateAuthed {
		return nil, fmt.Errorf("sdk_event: link %q not authed (state=%s), call LoginLink first", linkID, state)
	}
	return bc, nil
}

// buildAndConnect 创建 BeastClient 并 Connect（CreateLink 内部用）。
// 调用方必须已持 s.mu。
func (s *SdkEventService) buildAndConnect(params ConnectParams) (*beastcli.BeastClient, beastcli.Type, error) {
	transportType := beastcli.Type(params.Transport)
	if transportType == "" {
		transportType = beastcli.TypeTCP
	}

	bc := beastcli.New(s.log)
	if err := bc.Initialize(context.Background()); err != nil {
		return nil, "", fmt.Errorf("sdk_event: initialize: %w", err)
	}
	connCfg := beastcli.ConnectConfig{
		Transport: transportType,
		Host:      params.Host,
		Port:      params.Port,
		Timeout:   5 * time.Second,
	}
	if transportType == beastcli.TypeTLS {
		if params.TLS == nil || params.TLS.CAPath == "" {
			return nil, "", fmt.Errorf("sdk_event: transport=tls requires TLS.CAPath")
		}
		connCfg.TLS = &beastcli.TLSConfig{
			CAPath:      params.TLS.CAPath,
			ServerName:  params.TLS.ServerName,
			CertPath:    params.TLS.CertPath,
			KeyPath:     params.TLS.KeyPath,
			MinVersion:  0, // 0 让 transport 内部走默认 TLSv1.2
		}
	}
	if err := bc.Connect(connCfg); err != nil {
		return nil, "", fmt.Errorf("sdk_event: connect: %w", err)
	}
	return bc, transportType, nil
}

// msToDuration ms<=0 时返回 0（让 beastclient 内部走 DefaultTimeout）。
func msToDuration(ms int) time.Duration {
	if ms <= 0 {
		return 0
	}
	return time.Duration(ms) * time.Millisecond
}
