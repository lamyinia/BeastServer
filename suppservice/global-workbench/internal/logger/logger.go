// Package logger 实现标签页级日志路由。
//
// 设计目标：
//   - 代码任意位置 `logger.For("tcp-ping-pong").Info(...)` 即可精准打到对应标签页
//   - 每个测试页面挂载时注册自己的 tag
//   - 预设一批系统 tag（见 tags.go）
//   - 日志走 bus，前端订阅 channel 转发到 Wails Events
package logger

import (
	"time"
)

// Level 日志级别。
type Level string

const (
	LevelDebug Level = "DEBUG"
	LevelInfo  Level = "INFO"
	LevelWarn  Level = "WARN"
	LevelError Level = "ERROR"
)

// LogEntry 单条日志，序列化后推到前端。
type LogEntry struct {
	Tag    string         `json:"tag"`            // 标签页标识，如 "tcp-ping-pong" / "target.go" / "system"
	Level  Level          `json:"level"`          // DEBUG / INFO / WARN / ERROR
	Msg    string         `json:"msg"`            // 日志消息
	Ts     int64          `json:"ts"`             // unix ms
	Fields map[string]any `json:"fields,omitempty"` // 结构化字段（可选）
}

// Logger 是 tag-bound 的日志器。同一 tag 可创建多个实例，互相独立。
type Logger struct {
	tag string
}

// For 返回绑定到指定 tag 的 Logger。
// tag 应当与前端日志窗口的 tab 一一对应（见 tags.go 预设）。
func For(tag string) *Logger {
	return &Logger{tag: tag}
}

// Tag 返回 Logger 绑定的 tag。用于需要在业务代码里反查的场景。
func (l *Logger) Tag() string { return l.tag }

// Debug / Info / Warn / Error 四个级别，msg 是人类可读消息，fields 是结构化字段（可选）。
//
// 用法：
//   logger.For("target.go").Info("connect ok", map[string]any{"host": host, "port": port})
//   logger.For("transport.tcp").Error("read fail", map[string]any{"err": err.Error()})
func (l *Logger) Debug(msg string, fields ...map[string]any) {
	l.emit(LevelDebug, msg, fields)
}

func (l *Logger) Info(msg string, fields ...map[string]any) {
	l.emit(LevelInfo, msg, fields)
}

func (l *Logger) Warn(msg string, fields ...map[string]any) {
	l.emit(LevelWarn, msg, fields)
}

func (l *Logger) Error(msg string, fields ...map[string]any) {
	l.emit(LevelError, msg, fields)
}

// emit 构造 LogEntry 并推到 bus。
// fields 多个 map 时按顺序合并（后者覆盖前者）。
func (l *Logger) emit(level Level, msg string, fields []map[string]any) {
	entry := LogEntry{
		Tag:   l.tag,
		Level: level,
		Msg:   msg,
		Ts:    time.Now().UnixMilli(),
	}
	// 合并 fields（仅在有 fields 时分配 map，避免空 map 污染 JSON omitempty）
	if len(fields) > 0 {
		merged := make(map[string]any, 4)
		for _, f := range fields {
			for k, v := range f {
				merged[k] = v
			}
		}
		entry.Fields = merged
	}
	defaultBus.Publish(entry)
}
