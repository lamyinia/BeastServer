<script lang="ts" setup>
// v1 工具栏占位：用于放置 Target/Transport/Host/Port/Token 等
// 全局连接控制组件（v1-design.md §6.1）。
// 当前 v1 阶段还没有全局连接模型，留空作为视觉占位。
</script>

<template>
  <div class="toolbar">
    <span class="placeholder">全局工具栏（v1 占位）</span>
  </div>
</template>

<style scoped>
.toolbar {
  height: 32px;
  flex-shrink: 0;
  background: var(--bg-card);
  border-bottom: 1px solid var(--border-soft);
  display: flex;
  align-items: center;
  padding: 0 16px;
  font-size: 12px;
  color: var(--text-faint);
  font-style: italic;
}
.placeholder {
  opacity: 0.7;
}
</style>
