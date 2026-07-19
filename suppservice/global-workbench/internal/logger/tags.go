package logger

// 预设 tag 常量。
//
// 这些 tag 在工作台启动时就激活，不需要测试页面主动注册。
// 测试页面专用 tag（如 "tcp-ping-pong"）由前端 onMounted 时注册到 logs store。
//
// 命名约定：
//   - 系统级：单段，如 "system"
//   - 模块级：点分，如 "target.go" / "transport.tcp"，方便前端按模块分组
const (
	TagSystem       = "system"        // 启动 / 配置 / 致命错误
	TagTargetGo     = "target.go"     // Go target 状态机
	TagTransportTCP = "transport.tcp" // TCP transport
	TagRoomctl      = "roomctl"       // gRPC RoomService 客户端
	TagRecorder     = "recorder"      // 录制器
	TagReplayer     = "replayer"      // 回放器
	TagServerctl    = "serverctl"     // 远程 beastserver 进程管控（SSH + 裸进程）
)

// SystemTags 返回所有预设系统 tag 列表。
// 前端可用此列表预创建对应的日志 tab。
func SystemTags() []string {
	return []string{
		TagSystem,
		TagTargetGo,
		TagTransportTCP,
		TagRoomctl,
		TagRecorder,
		TagReplayer,
		TagServerctl,
	}
}
