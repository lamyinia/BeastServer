<script lang="ts" setup>
import {useLogsStore} from '../stores/logs'
import {useThemeStore} from '../stores/theme'

const logs = useLogsStore()
const theme = useThemeStore()

function showLogs() {
  logs.toggleLogWindow()
}

async function onThemeChange(ev: Event) {
  const v = (ev.target as HTMLSelectElement).value as 'elvish' | 'holy'
  await theme.setTheme(v)
}
</script>

<template>
  <header class="top-bar">
    <div class="brand">
      <span class="brand-mark">✦</span>
      <span class="brand-name">BeastServer</span>
      <span class="brand-sub">幻境工作台</span>
    </div>
    <div class="actions">
      <!-- 主题切换器：下拉框 -->
      <select
        class="theme-select"
        :value="theme.theme"
        @change="onThemeChange"
        title="切换主题"
        aria-label="切换主题"
      >
        <option v-for="t in theme.all" :key="t.name" :value="t.name">
          {{ t.icon }} {{ t.label }}
        </option>
      </select>
      <button class="gw-btn-secondary" @click="showLogs" title="切换日志窗口">
        {{ logs.logWindowVisible ? '隐藏日志' : '显示日志' }}
      </button>
    </div>
  </header>
</template>

<style scoped>
.top-bar {
  height: 44px;
  flex-shrink: 0;
  background: linear-gradient(180deg, var(--bg-secondary) 0%, var(--bg-card) 100%);
  border-bottom: 1px solid var(--border-soft);
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 0 16px;
  box-shadow: 0 1px 4px rgba(0, 0, 0, 0.3);
}

.brand {
  display: flex;
  align-items: baseline;
  gap: 8px;
}
.brand-mark {
  color: var(--border-strong);
  font-size: 16px;
  text-shadow: 0 0 8px var(--accent-glow);
}
.brand-name {
  font-family: var(--font-display);
  font-size: 14px;
  font-weight: 700;
  letter-spacing: 0.12em;
  color: var(--text-main);
  text-transform: uppercase;
}
.brand-sub {
  font-family: var(--font-display);
  font-size: 11px;
  color: var(--text-muted);
  letter-spacing: 0.2em;
  opacity: 0.8;
}

.actions {
  display: flex;
  align-items: center;
  gap: 12px;
}

/* 主题下拉框 */
.theme-select {
  width: auto;
  min-width: 140px;
  /* 用全局 height: 32px（来自 style.css），不在这里覆盖 */
  padding: 0 24px 0 10px;
  font-size: 12px;
  font-family: var(--font-body);
  cursor: pointer;
  /* 用主题色边框，凸显这是主题选择器 */
  border-color: var(--border-main);
}
.theme-select:hover {
  border-color: var(--border-strong);
  box-shadow: var(--shadow-glow);
}
.theme-select:focus {
  border-color: var(--border-strong);
  box-shadow: 0 0 0 2px var(--accent-glow);
}
</style>
