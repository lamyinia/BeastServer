<script lang="ts" setup>
import {computed} from 'vue'
import {useLogsStore} from '../stores/logs'
import LogTab from './LogTab.vue'

const store = useLogsStore()

// 当前激活 tab 的条目（store 已经过滤了 levelFilter）
const entries = computed(() => store.activeEntries)
const tabs = computed(() => store.tabs)
const activeTag = computed(() => store.activeTag)
const counts = computed(() => store.entryCountByTag)

function selectTag(tag: string) {
  store.setActiveTag(tag)
}

function closeTag(tag: string, e: MouseEvent) {
  e.stopPropagation()
  store.unregisterTag(tag)
}

function clearActive() {
  store.clearActive()
}

function toggleCollapse() {
  store.toggleCollapsed()
}

function hide() {
  store.toggleLogWindow()
}

// 级别过滤快捷切换
function setLevelFilter(l: 'DEBUG' | 'INFO' | 'WARN' | 'ERROR' | null) {
  store.levelFilter = store.levelFilter === l ? null : l
}

const collapsed = computed(() => store.logWindowCollapsed)
</script>

<template>
  <div class="log-window" :class="{collapsed}">
    <!-- Tab 头部条 -->
    <div class="tab-bar">
      <div class="tab-list">
        <button
          v-for="t in tabs"
          :key="t.tag"
          class="tab"
          :class="{active: t.tag === activeTag}"
          @click="selectTag(t.tag)"
          :title="t.tag"
        >
          <span class="icon" v-if="t.icon">{{ t.icon }}</span>
          <span class="label">{{ t.label }}</span>
          <span class="count" v-if="counts[t.tag]">{{ counts[t.tag] }}</span>
          <span
            v-if="!t.system"
            class="close"
            @click="closeTag(t.tag, $event)"
            title="关闭"
          >×</span>
        </button>
      </div>
      <div class="tab-actions">
        <select
          :value="store.levelFilter ?? ''"
          @change="setLevelFilter(($event.target as HTMLSelectElement).value as any || null)"
          title="级别过滤"
          class="filter-select"
        >
          <option value="">全部</option>
          <option value="DEBUG">DEBUG+</option>
          <option value="INFO">INFO+</option>
          <option value="WARN">WARN+</option>
          <option value="ERROR">ERROR</option>
        </select>
        <button class="icon-btn" @click="clearActive" title="清空当前 tab">清空</button>
        <button class="icon-btn" @click="toggleCollapse" :title="collapsed ? '展开' : '收起'">
          {{ collapsed ? '▲' : '▼' }}
        </button>
        <button class="icon-btn" @click="hide" title="隐藏日志窗口">✕</button>
      </div>
    </div>
    <!-- Tab 内容 -->
    <div v-show="!collapsed" class="tab-content">
      <LogTab :entries="entries"/>
    </div>
  </div>
</template>

<style scoped>
.log-window {
  background: var(--bg-secondary);
  border-top: 1px solid var(--border-soft);
  display: flex;
  flex-direction: column;
  height: 240px;
  flex-shrink: 0;
  transition: height 0.15s;
}
.log-window.collapsed {
  height: 28px;
}

.tab-bar {
  display: flex;
  align-items: center;
  background: var(--bg-card);
  border-bottom: 1px solid var(--border-soft);
  height: 28px;
  flex-shrink: 0;
}

.tab-list {
  flex: 1;
  display: flex;
  align-items: center;
  overflow-x: auto;
  overflow-y: hidden;
  height: 100%;
  scrollbar-width: none;
}
.tab-list::-webkit-scrollbar {
  height: 2px;
}

.tab {
  display: inline-flex;
  align-items: center;
  gap: 4px;
  height: 100%;
  padding: 0 10px;
  background: transparent;
  border: none;
  border-right: 1px solid var(--border-soft);
  border-radius: 0;
  color: var(--text-muted);
  font-family: var(--font-body);
  font-size: 12px;
  cursor: pointer;
  transition: background 0.15s, color 0.15s;
  white-space: nowrap;
  position: relative;
}
.tab:hover {
  background: var(--bg-elevated);
  color: var(--text-main);
  box-shadow: none;
}
.tab.active {
  background: var(--bg-inset);
  color: var(--border-strong);
  box-shadow: inset 0 -2px 0 var(--border-strong);
}
.tab .icon {
  font-size: 11px;
  opacity: 0.8;
}
.tab .count {
  background: var(--bg-elevated);
  color: var(--text-muted);
  font-size: 10px;
  padding: 0 5px;
  border-radius: 8px;
  min-width: 14px;
  text-align: center;
}
.tab.active .count {
  background: var(--accent);
  color: var(--text-main);
}
.tab .close {
  margin-left: 2px;
  font-size: 14px;
  line-height: 1;
  opacity: 0.5;
  padding: 0 2px;
}
.tab .close:hover {
  opacity: 1;
  color: var(--error);
}

.tab-actions {
  display: flex;
  align-items: center;
  gap: 4px;
  padding: 0 8px;
  flex-shrink: 0;
}

.filter-select {
  width: auto;
  padding: 2px 6px;
  font-size: 11px;
  height: 22px;
}

.icon-btn {
  background: transparent;
  border: 1px solid transparent;
  color: var(--text-muted);
  font-size: 11px;
  padding: 3px 8px;
  height: 22px;
  border-radius: var(--radius-sm);
}
.icon-btn:hover {
  background: var(--bg-elevated);
  color: var(--text-main);
  box-shadow: none;
  border-color: var(--border-soft);
}

.tab-content {
  flex: 1;
  overflow: hidden;
  background: var(--bg-inset);
}
</style>
