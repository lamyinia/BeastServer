import {defineStore} from 'pinia'
import {ref} from 'vue'
import * as ServerctlService from '../../bindings/global-workbench/serverctlservice.js'
import {useLogsStore} from './logs'

// ============================================================================
// serverctl store
// ----------------------------------------------------------------------------
// 把"远端日志自动拉取"的状态 + 定时器从页面级组件提到 store，
// 这样切到其他页面（如 go-sdk 自检）时定时器不会被 onUnmounted 清理，
// LogWindow 在任意页面都能持续看到 serverctl 日志。
//
// 设计要点：
//   - timer ref 持有 setInterval 句柄，store 销毁时（应用退出）才清理
//   - running ref 防止上次拉取还没回来就发起下次
//   - lastError 暴露给 UI 显示失败原因
//   - 拉取到的日志直接 push 进 logs store 的 serverctl tab
// ============================================================================

export const useServerctlStore = defineStore('serverctl', () => {
  const logs = useLogsStore()

  const autoRefresh = ref(false)
  const logLines = ref(200)
  const running = ref(false)
  const lastError = ref('')
  const lastMsg = ref('')

  let timer: number | null = null

  async function refreshOnce() {
    if (running.value) return
    running.value = true
    lastError.value = ''
    try {
      const out = await ServerctlService.Logs(logLines.value)
      if (out) {
        const lines = out.split('\n').filter((l: string) => l.length > 0)
        for (const line of lines) {
          logs.pushEntry({
            tag: 'serverctl',
            level: 'INFO',
            msg: line,
            ts: Date.now(),
          })
        }
        lastMsg.value = `已拉取 ${logLines.value} 行原始日志`
      }
    } catch (e: any) {
      lastError.value = `${e?.message ?? e}`
      logs.pushEntry({
        tag: 'serverctl',
        level: 'ERROR',
        msg: '拉取日志失败',
        ts: Date.now(),
        fields: {err: String(e?.message ?? e)},
      })
    } finally {
      running.value = false
    }
  }

  function startAutoRefresh() {
    if (timer !== null) return  // 已在跑
    autoRefresh.value = true
    timer = window.setInterval(refreshOnce, 2000)
  }

  function stopAutoRefresh() {
    autoRefresh.value = false
    if (timer !== null) {
      window.clearInterval(timer)
      timer = null
    }
  }

  function toggleAutoRefresh(v: boolean) {
    if (v) startAutoRefresh()
    else stopAutoRefresh()
  }

  return {
    // state
    autoRefresh,
    logLines,
    running,
    lastError,
    lastMsg,
    // actions
    refreshOnce,
    startAutoRefresh,
    stopAutoRefresh,
    toggleAutoRefresh,
  }
})
