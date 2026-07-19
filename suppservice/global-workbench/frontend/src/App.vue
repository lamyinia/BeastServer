<script lang="ts" setup>
import {computed} from 'vue'
import TopBar from './components/TopBar.vue'
import Toolbar from './components/Toolbar.vue'
import Sidebar from './components/Sidebar.vue'
import LogWindow from './components/LogWindow.vue'
import {useLogsStore} from './stores/logs'

const logs = useLogsStore()
const logWindowVisible = computed(() => logs.logWindowVisible)
</script>

<template>
  <div class="app-shell">
    <!-- 顶部品牌栏 -->
    <TopBar/>
    <!-- 全局工具栏（v1 占位） -->
    <Toolbar/>
    <!-- 主区：左 sidebar + 右 page -->
    <div class="main-area">
      <Sidebar/>
      <main class="page-area">
        <RouterView/>
      </main>
    </div>
    <!-- 底部 LogWindow dock（可隐藏） -->
    <LogWindow v-if="logWindowVisible"/>
  </div>
</template>

<style scoped>
.app-shell {
  display: flex;
  flex-direction: column;
  height: 100vh;
  overflow: hidden;
}

.main-area {
  flex: 1;
  display: flex;
  overflow: hidden;
  min-height: 0; /* 关键：让子元素可以收缩滚动 */
}

.page-area {
  flex: 1;
  overflow-y: auto;
  background: var(--bg-primary);
  min-width: 0;
}
</style>
