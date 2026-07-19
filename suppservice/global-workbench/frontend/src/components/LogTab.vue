<script lang="ts" setup>
import {computed, ref, watch, nextTick} from 'vue'
import type {LogEntry, LogLevel} from '../stores/logs'

const props = defineProps<{
  entries: LogEntry[]
}>()

// 自动滚动到底部开关
const autoScroll = ref(true)
const scrollEl = ref<HTMLDivElement | null>(null)

// 滚到底部
async function scrollToBottom() {
  if (!autoScroll.value) return
  await nextTick()
  const el = scrollEl.value
  if (el) el.scrollTop = el.scrollHeight
}

watch(() => props.entries.length, () => {
  scrollToBottom()
})

// 格式化时间戳
function fmtTs(ts: number): string {
  const d = new Date(ts)
  const pad = (n: number, l = 2) => String(n).padStart(l, '0')
  return `${pad(d.getHours())}:${pad(d.getMinutes())}:${pad(d.getSeconds())}.${pad(d.getMilliseconds(), 3)}`
}

// 级别对应的 class
function levelClass(l: LogLevel): string {
  return `lvl-${l.toLowerCase()}`
}

// 把 fields map 序列化成 k=v 字符串
function fmtFields(fields?: Record<string, unknown>): string {
  if (!fields) return ''
  const parts: string[] = []
  for (const [k, v] of Object.entries(fields)) {
    let s: string
    if (typeof v === 'string') s = v
    else if (v === null) s = 'null'
    else if (v === undefined) s = 'undefined'
    else try { s = JSON.stringify(v) } catch { s = String(v) }
    parts.push(`${k}=${s}`)
  }
  return parts.length ? '  ' + parts.join(' ') : ''
}

// 占位：空列表文案
const emptyText = computed(() => '(暂无日志)')
</script>

<template>
  <div class="log-tab">
    <div ref="scrollEl" class="log-scroll">
      <div v-if="entries.length === 0" class="log-empty">{{ emptyText }}</div>
      <div
        v-for="(e, i) in entries"
        :key="i"
        class="log-line"
        :class="levelClass(e.level)"
      >
        <span class="ts">{{ fmtTs(e.ts) }}</span>
        <span class="lvl">{{ e.level }}</span>
        <span class="msg">{{ e.msg }}<span class="fields">{{ fmtFields(e.fields) }}</span></span>
      </div>
    </div>
  </div>
</template>

<style scoped>
.log-tab {
  height: 100%;
  display: flex;
  flex-direction: column;
  background: var(--bg-inset);
  overflow: hidden;
}
.log-scroll {
  flex: 1;
  overflow-y: auto;
  padding: 6px 10px;
  font-family: var(--font-mono);
  font-size: 12px;
  line-height: 1.55;
}
.log-empty {
  color: var(--text-faint);
  font-style: italic;
  padding: 12px 4px;
}
.log-line {
  display: flex;
  align-items: flex-start;
  gap: 6px;
  padding: 1px 0;
  word-break: break-all;
}
.log-line .ts {
  color: var(--text-faint);
  flex-shrink: 0;
  user-select: none;
}
.log-line .lvl {
  flex-shrink: 0;
  font-weight: 600;
  width: 50px;
  user-select: none;
}
.log-line .msg {
  flex: 1;
  color: var(--text-main);
  white-space: pre-wrap;
}
.log-line .fields {
  color: var(--text-muted);
  font-weight: normal;
}

/* 级别配色（精灵幻境风：紫水晶系） */
.log-line.lvl-debug .lvl { color: var(--text-faint); }
.log-line.lvl-debug .msg { color: var(--text-muted); }
.log-line.lvl-info  .lvl { color: var(--info); }
.log-line.lvl-warn  .lvl { color: var(--warn); }
.log-line.lvl-warn  .msg { color: var(--warn); }
.log-line.lvl-error .lvl { color: var(--error); }
.log-line.lvl-error .msg { color: var(--error); }
.log-line.lvl-error {
  background: rgba(244, 114, 182, 0.06);
  border-left: 2px solid var(--error);
  padding-left: 4px;
  margin-left: -6px;
}
</style>
