<script lang="ts" setup>
import {useRouter, useRoute} from 'vue-router'
import {computed} from 'vue'

const router = useRouter()
const route = useRoute()

interface TreeNode {
  label: string
  icon?: string
  // 叶子节点：路由 name
  routeName?: string
  // 分组节点：子节点
  children?: TreeNode[]
  // 路径标识（用于展开/激活态 key）
  key: string
}

// 树形目录（v1 只放 beastserver 控制台 + 几个占位分组）
// 占位分组后续 v2/v3 加叶子节点。
const tree: TreeNode[] = [
  {
    key: 'home',
    label: '首页',
    icon: '⌂',
    routeName: 'dashboard',
  },
  {
    key: 'beastserver',
    label: 'BeastServer',
    icon: '◈',
    children: [
      {
        key: 'beastserver/control',
        label: '进程控制',
        icon: '⏻',
        routeName: 'server-control',
      },
    ],
  },
  {
    key: 'scenes',
    label: '玩法场景',
    icon: '✦',
    children: [
      {key: 'scenes/match', label: '一局完整对局', icon: '∅'},
    ],
  },
  {
    key: 'load',
    label: '并发压测',
    icon: '⚡',
    children: [
      {key: 'load/conn', label: '连接压测', icon: '∅'},
    ],
  },
  {
    key: 'sdk',
    label: 'SDK 自检',
    icon: '◇',
    children: [
      {key: 'sdk/event', label: 'go-sdk 自检', icon: '◉', routeName: 'sdk-event'},
    ],
  },
]

const activeKey = computed(() => String(route.name ?? ''))

function go(node: TreeNode) {
  if (node.routeName) {
    router.push({name: node.routeName})
  }
}
</script>

<template>
  <aside class="sidebar">
    <nav class="tree">
      <template v-for="node in tree" :key="node.key">
        <!-- 叶子节点：直接是可点击项 -->
        <a
          v-if="!node.children"
          class="leaf"
          :class="{active: activeKey === node.routeName}"
          @click="go(node)"
        >
          <span class="icon">{{ node.icon }}</span>
          <span class="label">{{ node.label }}</span>
        </a>
        <!-- 分组节点 -->
        <div v-else class="group">
          <div class="group-head">
            <span class="icon">{{ node.icon }}</span>
            <span class="label">{{ node.label }}</span>
          </div>
          <div class="group-children">
            <a
              v-for="c in node.children"
              :key="c.key"
              class="leaf"
              :class="{
                active: c.routeName && activeKey === c.routeName,
                disabled: !c.routeName,
              }"
              @click="go(c)"
            >
              <span class="icon">{{ c.icon }}</span>
              <span class="label">{{ c.label }}</span>
            </a>
          </div>
        </div>
      </template>
    </nav>
    <div class="sidebar-foot">
      <span class="version">v0.1 · Elvish Wonder</span>
    </div>
  </aside>
</template>

<style scoped>
.sidebar {
  width: 220px;
  flex-shrink: 0;
  background: var(--bg-secondary);
  border-right: 1px solid var(--border-soft);
  display: flex;
  flex-direction: column;
  overflow: hidden;
}
.tree {
  flex: 1;
  overflow-y: auto;
  padding: 10px 6px;
}
.leaf, .group-head {
  display: flex;
  align-items: center;
  gap: 8px;
  padding: 5px 10px;
  font-size: 13px;
  color: var(--text-muted);
  border-radius: var(--radius-sm);
  cursor: pointer;
  transition: background 0.12s, color 0.12s;
  user-select: none;
}
.leaf:hover {
  background: var(--bg-elevated);
  color: var(--text-main);
}
.leaf.active {
  background: linear-gradient(90deg, var(--accent-glow) 0%, transparent 100%);
  color: var(--border-strong);
  border-left: 2px solid var(--border-strong);
  padding-left: 8px;
}
.leaf.disabled {
  opacity: 0.4;
  cursor: default;
}
.leaf.disabled:hover {
  background: transparent;
  color: var(--text-muted);
}
.leaf .icon, .group-head .icon {
  width: 14px;
  text-align: center;
  font-size: 12px;
  opacity: 0.7;
}

.group {
  margin-bottom: 4px;
}
.group-head {
  font-family: var(--font-display);
  font-size: 12px;
  font-weight: 700;
  letter-spacing: 0.08em;
  color: var(--text-muted);
  text-transform: uppercase;
  cursor: default;
  padding-top: 8px;
  padding-bottom: 4px;
  border-radius: 0;
}
.group-head:hover {
  background: transparent;
  color: var(--text-muted);
}
.group-head .label {
  flex: 1;
}
.group-children {
  padding-left: 12px;
  border-left: 1px dashed var(--border-soft);
  margin-left: 14px;
}

.sidebar-foot {
  padding: 6px 12px;
  border-top: 1px solid var(--border-soft);
  font-size: 11px;
  color: var(--text-faint);
  text-align: center;
  font-family: var(--font-display);
  letter-spacing: 0.1em;
}
</style>
