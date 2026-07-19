<script lang="ts" setup>
import {useRouter} from 'vue-router'
import {useLogsStore} from '../stores/logs'

const router = useRouter()
const logs = useLogsStore()

interface QuickEntry {
  title: string
  desc: string
  icon: string
  route?: string
  // 点击是否切换到指定日志 tab
  logTag?: string
  accent?: 'primary' | 'success' | 'error'
}

const entries: QuickEntry[] = [
  {
    title: 'BeastServer 进程控制',
    desc: '启动 / 停止 / 重启远端 beastserver，查看进程状态',
    icon: '⏻',
    route: '/server/control',
    logTag: 'serverctl',
    accent: 'primary',
  },
  {
    title: '连接压测',
    desc: '高并发连接建立 / 心跳承载',
    icon: '⚡',
    accent: 'primary',
  },
  {
    title: 'SDK 路由表同步',
    desc: '从 proto 同步 routes 到客户端常量',
    icon: '◇',
    accent: 'primary',
  },
]

function go(e: QuickEntry) {
  if (e.route) {
    router.push(e.route)
  }
  if (e.logTag) {
    logs.setActiveTag(e.logTag)
    // 显示日志窗口（如果被隐藏）
    if (!logs.logWindowVisible) {
      logs.toggleLogWindow()
    }
  }
}
</script>

<template>
  <div class="dashboard">
    <header class="hero">
      <h1 class="hero-title">幻境工作台</h1>
      <p class="hero-sub">BeastServer Global Workbench · 端到端联调与回放</p>
    </header>

    <section class="quick-grid">
      <article
        v-for="e in entries"
        :key="e.title"
        class="entry-card"
        :class="`accent-${e.accent ?? 'primary'}`"
        @click="go(e)"
      >
        <div class="entry-icon">{{ e.icon }}</div>
        <div class="entry-body">
          <div class="entry-title">{{ e.title }}</div>
          <div class="entry-desc">{{ e.desc }}</div>
        </div>
        <div class="entry-arrow" v-if="e.route">→</div>
      </article>
    </section>

    <section class="hint">
      <p>左侧侧边栏可导航到各功能页。底部日志窗口按 tag 分流，点击上方卡片自动切换对应 tab。</p>
    </section>
  </div>
</template>

<style scoped>
.dashboard {
  padding: 24px 32px;
  max-width: 1200px;
  margin: 0 auto;
}

.hero {
  margin-bottom: 28px;
  text-align: center;
  padding: 24px 0 16px;
  border-bottom: 1px solid var(--border-soft);
}
.hero-title {
  font-family: var(--font-display);
  font-size: 28px;
  font-weight: 700;
  letter-spacing: 0.15em;
  color: var(--border-strong);
  text-shadow: 0 0 12px var(--accent-glow);
  text-transform: uppercase;
  margin-bottom: 8px;
}
.hero-sub {
  font-size: 13px;
  color: var(--text-muted);
  letter-spacing: 0.05em;
  margin: 0;
}

.quick-grid {
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(280px, 1fr));
  gap: 14px;
}

.entry-card {
  background: var(--bg-card);
  border: 1px solid var(--border-soft);
  border-radius: var(--radius-md);
  padding: 14px 16px;
  display: flex;
  align-items: center;
  gap: 14px;
  cursor: pointer;
  transition: all 0.18s;
  position: relative;
  overflow: hidden;
}
.entry-card::before {
  content: '';
  position: absolute;
  top: 0;
  left: 0;
  right: 0;
  height: 1px;
  background: linear-gradient(90deg, transparent 0%, var(--border-strong) 50%, transparent 100%);
  opacity: 0;
  transition: opacity 0.2s;
}
.entry-card:hover {
  border-color: var(--border-main);
  background: var(--bg-elevated);
  transform: translateY(-1px);
  box-shadow: 0 4px 12px rgba(0, 0, 0, 0.3);
}
.entry-card:hover::before {
  opacity: 1;
}

.entry-icon {
  width: 36px;
  height: 36px;
  border-radius: var(--radius-md);
  background: var(--bg-inset);
  border: 1px solid var(--border-soft);
  display: flex;
  align-items: center;
  justify-content: center;
  font-size: 18px;
  color: var(--border-strong);
  flex-shrink: 0;
}

.entry-body {
  flex: 1;
  min-width: 0;
}
.entry-title {
  font-family: var(--font-display);
  font-size: 13px;
  font-weight: 700;
  letter-spacing: 0.06em;
  color: var(--text-main);
  margin-bottom: 2px;
}
.entry-desc {
  font-size: 12px;
  color: var(--text-muted);
  line-height: 1.4;
  overflow: hidden;
  text-overflow: ellipsis;
  display: -webkit-box;
  -webkit-line-clamp: 2;
  -webkit-box-orient: vertical;
}

.entry-arrow {
  color: var(--text-faint);
  font-size: 16px;
  transition: transform 0.15s, color 0.15s;
}
.entry-card:hover .entry-arrow {
  color: var(--border-strong);
  transform: translateX(2px);
}

.hint {
  margin-top: 24px;
  padding: 12px;
  background: var(--bg-inset);
  border: 1px dashed var(--border-soft);
  border-radius: var(--radius-md);
  font-size: 12px;
  color: var(--text-muted);
  font-style: italic;
  text-align: center;
}
.hint p { margin: 0; }
</style>
