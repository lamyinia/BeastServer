import {defineStore} from 'pinia'
import {ref, computed, shallowRef} from 'vue'
import {Events} from '@wailsio/runtime'

// ============================================================================
// 日志 store
// ----------------------------------------------------------------------------
// 设计目标（对齐 v1-design.md §5.5）：
//   - 按 tag 字段路由：每条 LogEntry 落到对应 tag 的环形缓冲里
//   - 每 tab 最多保留 5000 条（FIFO 丢弃旧条目）
//   - 启动时预创建 7 个系统 tag 的 tab
//   - 测试页面挂载时可调用 registerTag 动态新增 tab
//   - Wails 后端通过 Events.Emit("log:entry", entry) 推送（main.go goroutine 桥接）
// ============================================================================

export type LogLevel = 'DEBUG' | 'INFO' | 'WARN' | 'ERROR'

export interface LogEntry {
  tag: string
  level: LogLevel
  msg: string
  ts: number // unix ms
  fields?: Record<string, unknown>
}

export interface TabMeta {
  tag: string
  label: string
  icon?: string
  // 是否是系统 tag（不可移除）
  system: boolean
}

// 单 tab 环形缓冲上限。超出后丢弃最旧的条目。
const MAX_ENTRIES_PER_TAB = 5000

// 7 个预设系统 tag（与 internal/logger/tags.go 保持一致）
const SYSTEM_TAGS: TabMeta[] = [
  {tag: 'system', label: '系统', icon: '⚙', system: true},
  {tag: 'target.go', label: 'Go Target', icon: '🎯', system: true},
  {tag: 'transport.tcp', label: 'TCP 传输', icon: '🚚', system: true},
  {tag: 'roomctl', label: 'Room 控制器', icon: '🚪', system: true},
  {tag: 'recorder', label: '录制器', icon: '⏺', system: true},
  {tag: 'replayer', label: '回放器', icon: '⏪', system: true},
  {tag: 'serverctl', label: '服务端进程', icon: '🖥', system: true},
]

// LogWindow 是否显示（全局开关，App.vue 控制）
const logWindowVisible = ref(true)
// LogWindow 是否折叠（仅显示 tab 头）
const logWindowCollapsed = ref(false)
// 当前激活的 tab tag
const activeTag = ref<string>('system')
// 所有 tab 元信息（保序）
const tabs = ref<TabMeta[]>([...SYSTEM_TAGS])
// 日志数据：tag → 环形缓冲条目数组
// 用 shallowRef + 手动 trigger 来避免大数组深度响应式开销
const entriesByTag = ref<Record<string, LogEntry[]>>({})
// 7 个系统 tag 预初始化空数组
for (const t of SYSTEM_TAGS) {
  entriesByTag.value[t.tag] = []
}

// 当前 tab 的过滤级别（DEBUG/INFO/WARN/ERROR）。null=不过滤。
const levelFilter = ref<LogLevel | null>(null)

// === 计算属性 ===
const activeEntries = computed<LogEntry[]>(() => {
  const list = entriesByTag.value[activeTag.value] ?? []
  const filter = levelFilter.value
  if (filter === null) return list
  return list.filter(e => levelRank(e.level) >= levelRank(filter))
})

const activeTab = computed<TabMeta | undefined>(() =>
  tabs.value.find(t => t.tag === activeTag.value),
)

// 每个标签的条目数（用于 tab 头角标）
const entryCountByTag = computed<Record<string, number>>(() => {
  const result: Record<string, number> = {}
  for (const t of tabs.value) {
    result[t.tag] = (entriesByTag.value[t.tag] ?? []).length
  }
  return result
})

// === 动作 ===
function levelRank(l: LogLevel): number {
  switch (l) {
    case 'DEBUG': return 0
    case 'INFO':  return 1
    case 'WARN':  return 2
    case 'ERROR': return 3
  }
}

function setActiveTag(tag: string) {
  if (tabs.value.some(t => t.tag === tag)) {
    activeTag.value = tag
  }
}

function registerTag(meta: { tag: string; label: string; icon?: string }) {
  if (tabs.value.some(t => t.tag === meta.tag)) return
  tabs.value.push({...meta, system: false})
  // 用 Vue.set 等价方式：重新赋值触发响应
  entriesByTag.value = {...entriesByTag.value, [meta.tag]: []}
}

function unregisterTag(tag: string) {
  // 系统 tag 不可移除
  if (SYSTEM_TAGS.some(t => t.tag === tag)) return
  tabs.value = tabs.value.filter(t => t.tag !== tag)
  const next = {...entriesByTag.value}
  delete next[tag]
  entriesByTag.value = next
  if (activeTag.value === tag) {
    activeTag.value = 'system'
  }
}

function pushEntry(entry: LogEntry) {
  // 未知 tag 自动注册（保险：后端新增 tag 时前端不崩）
  if (!tabs.value.some(t => t.tag === entry.tag)) {
    registerTag({tag: entry.tag, label: entry.tag, icon: '•'})
  }
  const list = entriesByTag.value[entry.tag] ?? (entriesByTag.value[entry.tag] = [])
  list.push(entry)
  // 环形缓冲：超限时丢弃最旧的
  if (list.length > MAX_ENTRIES_PER_TAB) {
    list.splice(0, list.length - MAX_ENTRIES_PER_TAB)
  }
}

function clearTag(tag: string) {
  if (entriesByTag.value[tag]) {
    entriesByTag.value[tag] = []
  }
}

function clearActive() {
  clearTag(activeTag.value)
}

function clearAll() {
  for (const t of tabs.value) {
    entriesByTag.value[t.tag] = []
  }
}

function toggleLogWindow() {
  logWindowVisible.value = !logWindowVisible.value
}

function toggleCollapsed() {
  logWindowCollapsed.value = !logWindowCollapsed.value
}

// === Wails 事件订阅 ===
// 后端 main.go 启动 goroutine 把 logger bus 的 entry 通过
// app.Event.Emit("log:entry", entry) 推过来，前端订阅后按 tag 路由。
let subscribed = false
function ensureSubscribed() {
  if (subscribed) return
  subscribed = true
  Events.On('log:entry', (ev: any) => {
    const entry = ev?.data as LogEntry | undefined
    if (entry && entry.tag && entry.level && entry.msg !== undefined) {
      pushEntry(entry)
    }
  })
}

// store 工厂
export const useLogsStore = defineStore('logs', () => {
  // 启动时立即订阅 Wails 事件
  ensureSubscribed()

  return {
    // state
    logWindowVisible,
    logWindowCollapsed,
    activeTag,
    tabs,
    entriesByTag,
    levelFilter,
    // getters
    activeEntries,
    activeTab,
    entryCountByTag,
    // actions
    setActiveTag,
    registerTag,
    unregisterTag,
    pushEntry,
    clearTag,
    clearActive,
    clearAll,
    toggleLogWindow,
    toggleCollapsed,
  }
})
