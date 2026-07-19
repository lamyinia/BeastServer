package logger

import (
	"fmt"
	"os"
	"sync"
)

// subscriberBufferSize 单个订阅者 channel 缓冲大小。
// 满了之后会丢弃新日志并写 stderr 警告（防止慢消费者阻塞生产者）。
const subscriberBufferSize = 1024

// Bus 日志总线：维护多个订阅者，把 Publish 的 LogEntry 推给所有订阅者。
//
// 默认全局实例 defaultBus 在本文件底部被 Logger.emit（见 logger.go）使用。
// 业务代码通常不需要直接操作 Bus，只用全局 Publish/Subscribe。
// 测试可用 NewBus 隔离。
type Bus struct {
	mu     sync.RWMutex
	subs   map[uint64]chan LogEntry
	nextID uint64
}

// NewBus 创建独立的 Bus 实例（用于隔离测试）。
func NewBus() *Bus {
	return &Bus{subs: make(map[uint64]chan LogEntry)}
}

// Publish 把 entry 推给所有订阅者。
// 订阅者 channel 满了会丢弃该 entry 并写 stderr（不阻塞生产者）。
func (b *Bus) Publish(entry LogEntry) {
	b.mu.RLock()
	defer b.mu.RUnlock()
	for _, ch := range b.subs {
		select {
		case ch <- entry:
		default:
			// 慢消费者：丢弃并写 stderr
			fmt.Fprintf(os.Stderr, "logger: subscriber channel full, dropping entry (tag=%s level=%s)\n",
				entry.Tag, entry.Level)
		}
	}
}

// Subscribe 订阅日志流，返回只读 channel。
// 当 Unsubscribe 被调用时，channel 会被关闭。
//
// 注意：返回的 channel 缓冲为 subscriberBufferSize（1024），
// 满了之后新日志会被丢弃并写 stderr。
func (b *Bus) Subscribe() <-chan LogEntry {
	return b.SubscribeWithBuffer(subscriberBufferSize)
}

// SubscribeWithBuffer 订阅并指定 channel 缓冲大小。
func (b *Bus) SubscribeWithBuffer(bufSize int) <-chan LogEntry {
	b.mu.Lock()
	defer b.mu.Unlock()
	id := b.nextID
	b.nextID++
	ch := make(chan LogEntry, bufSize)
	b.subs[id] = ch
	return ch
}

// Unsubscribe 取消订阅并关闭 channel。
// 调用方应当在收到 channel 关闭后停止 range。
func (b *Bus) Unsubscribe(ch <-chan LogEntry) {
	b.mu.Lock()
	defer b.mu.Unlock()
	for id, c := range b.subs {
		// 双向 channel 与单向 channel 可通过底层指针比较
		if c == ch {
			close(c)
			delete(b.subs, id)
			return
		}
	}
}

// SubscriberCount 返回当前订阅者数量（测试用）。
func (b *Bus) SubscriberCount() int {
	b.mu.RLock()
	defer b.mu.RUnlock()
	return len(b.subs)
}

// defaultBus 全局默认总线，所有 Logger.emit 默认推到这里。
var defaultBus = NewBus()

// Publish 全局便捷方法：推到 defaultBus。
func Publish(entry LogEntry) { defaultBus.Publish(entry) }

// Subscribe 全局便捷方法：订阅 defaultBus。
func Subscribe() <-chan LogEntry { return defaultBus.Subscribe() }

// Unsubscribe 全局便捷方法：取消订阅 defaultBus。
func Unsubscribe(ch <-chan LogEntry) { defaultBus.Unsubscribe(ch) }
