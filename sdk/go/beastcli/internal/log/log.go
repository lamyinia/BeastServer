// Package log 定义 beastcli 内部使用的 Logger 接口。
//
// 设计目标：
//   - beastcli/internal/* 各包（codec/transport/beastclient）依赖此接口记录日志
//   - 调用方注入具体实现（如工作台 internal/logger.For(tag)）
//   - 不注入时用 NopLogger，零开销
//
// 方法签名与工作台 internal/logger.Logger 一致，
// 这样工作台 logger.For(...) 可直接作为 beastcli 内部 log.Logger 使用（Go 鸭子类型）。
package log

// Logger beastcli 内部使用的结构化日志接口。
//
// fields 是可选的结构化字段，每个 map 会被合并到日志条目里。
// 方法签名严格对齐工作台 internal/logger.*Logger，
// 让工作台 *logger.Logger 可直接作为本接口的实现传入。
type Logger interface {
	Debug(msg string, fields ...map[string]any)
	Info(msg string, fields ...map[string]any)
	Warn(msg string, fields ...map[string]any)
	Error(msg string, fields ...map[string]any)
}

// NopLogger 啥也不做的 Logger，零开销。用于不需要日志的场景。
type NopLogger struct{}

func (NopLogger) Debug(string, ...map[string]any) {}
func (NopLogger) Info(string, ...map[string]any)  {}
func (NopLogger) Warn(string, ...map[string]any)  {}
func (NopLogger) Error(string, ...map[string]any) {}
