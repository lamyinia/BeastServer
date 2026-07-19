<script lang="ts" setup>
import {ref, onMounted, computed, watch} from 'vue'
import * as ServerctlService from '../../../bindings/global-workbench/serverctlservice.js'
import * as RoomctlService from '../../../bindings/global-workbench/roomctlservice.js'
import {useLogsStore} from '../../stores/logs'
import {useServerProfilesStore} from '../../stores/serverProfiles'
import {useServerctlStore} from '../../stores/serverctl'

const logs = useLogsStore()
const profiles = useServerProfilesStore()

// 通过 logger bus 写到 serverctl tab（后端 goroutine 桥接回前端）
// 这里使用 logs store 直接 push（前端→前端），用于操作反馈
function pushLog(level: 'INFO' | 'WARN' | 'ERROR', msg: string, fields?: Record<string, unknown>) {
  logs.pushEntry({
    tag: 'serverctl',
    level,
    msg,
    ts: Date.now(),
    fields,
  })
  logs.setActiveTag('serverctl')
}

// ============ 凭据（绑定到当前 profile） ============
const host = ref('')
const port = ref(22)
const user = ref('')
const password = ref('')  // 不再预填默认密码
const grpcPort = ref(9010)  // beastserver gRPC 端口（RoomService）

// dirty 标记：用户改了 input 但未应用，下拉框显示 *
const dirty = ref(false)

// 从当前 profile 加载凭据到 input
function loadFromProfile() {
  const p = profiles.current
  if (!p) return
  host.value = p.host
  port.value = p.port
  user.value = p.user
  password.value = p.password
  grpcPort.value = p.grpcPort ?? 9010
  dirty.value = false
}

// watch 当前 profile 切换 → 重新加载 input
watch(() => profiles.currentId, () => {
  loadFromProfile()
})

// 用户改 input 时标记 dirty
function markDirty() { dirty.value = true }

const applyCredsRunning = ref(false)
const applyCredsMsg = ref('')

// 内部：把当前 input 持久化到 profile + 调后端 SetCredentials。
// 仅在 dirty 时调（避免无谓的重复 SSH Dial 重置）。
// 如果凭据不完整（host/user/password 任一为空），抛错让调用方提示用户。
async function ensureCredsApplied() {
  if (!dirty.value) return  // 没 dirty，跳过
  if (!host.value || !user.value || !password.value) {
    throw new Error('请填写 host / user / password 后再操作')
  }
  profiles.commitCurrent({
    host: host.value,
    port: port.value,
    user: user.value,
    password: password.value,
    grpcPort: grpcPort.value,
  })
  dirty.value = false
  await ServerctlService.SetCredentials(host.value, port.value, user.value, password.value)
  // 同步把 gRPC 地址推给 RoomctlService
  await RoomctlService.SetAddr(`${host.value}:${grpcPort.value}`)
  pushLog('INFO', 'SSH 凭据已自动应用', {host: host.value, port: port.value, user: user.value, grpcPort: grpcPort.value, profile: profiles.current?.name})
}

async function applyCreds() {
  applyCredsRunning.value = true
  applyCredsMsg.value = ''
  try {
    // 如果没 dirty，强制重新 apply 一次（用户手动点按钮的情况）
    if (!dirty.value) {
      dirty.value = true
    }
    await ensureCredsApplied()
    applyCredsMsg.value = '凭据已应用（下次操作时 Dial）'
    // 应用后立即刷新一次状态
    await refreshStatus()
  } catch (e: any) {
    applyCredsMsg.value = `应用失败：${e?.message ?? e}`
    pushLog('ERROR', '应用凭据失败', {err: String(e?.message ?? e)})
  } finally {
    applyCredsRunning.value = false
  }
}

// Profile 管理
function onPickProfile(ev: Event) {
  const id = (ev.target as HTMLSelectElement).value
  profiles.setCurrent(id)
}
function newProfile() {
  profiles.create()
  // create 已经切到新 profile，watch 会触发 loadFromProfile
  pushLog('INFO', '新建服务器 profile', {name: profiles.current?.name})
}
function deleteProfile() {
  if (!profiles.current) return
  if (!confirm(`确认删除 profile "${profiles.current.name}"？`)) return
  const name = profiles.current.name
  profiles.remove(profiles.current.id)
  pushLog('INFO', '删除服务器 profile', {name})
}

// ============ 状态 ============
type StatusState = 'unknown' | 'running' | 'stopped'
const statusState = ref<StatusState>('unknown')
const statusPid = ref(0)
const statusRunning = ref(false)
const statusErr = ref('')

const statusDotColor = computed(() => {
  switch (statusState.value) {
    case 'running': return 'var(--success)'
    case 'stopped': return 'var(--error)'
    default:        return 'var(--text-faint)'
  }
})

const statusText = computed(() => {
  switch (statusState.value) {
    case 'running': return `running (pid=${statusPid.value})`
    case 'stopped': return 'stopped'
    default:        return 'unknown'
  }
})

async function refreshStatus() {
  statusRunning.value = true
  statusErr.value = ''
  try {
    await ensureCredsApplied()
    const [pid, st] = await ServerctlService.Status()
    statusPid.value = pid
    if (st === 'running')      statusState.value = 'running'
    else if (st === 'stopped') statusState.value = 'stopped'
    else                       statusState.value = 'unknown'
    pushLog('INFO', '查询状态', {pid, state: st})
  } catch (e: any) {
    statusState.value = 'unknown'
    statusErr.value = `${e?.message ?? e}`
    pushLog('ERROR', '查询状态失败', {err: String(e?.message ?? e)})
  } finally {
    statusRunning.value = false
  }
}

// ============ 操作按钮 ============
const startRunning = ref(false)
const stopRunning = ref(false)
const restartRunning = ref(false)
const opMsg = ref('')

const canStart = computed(() =>
    !startRunning.value && !stopRunning.value && !restartRunning.value
    && statusState.value !== 'running')
const canStop = computed(() =>
    !startRunning.value && !stopRunning.value && !restartRunning.value
    && statusState.value === 'running')
const canRestart = computed(() =>
    !startRunning.value && !stopRunning.value && !restartRunning.value
    && statusState.value === 'running')

async function start() {
  startRunning.value = true
  opMsg.value = ''
  try {
    await ensureCredsApplied()
    const pid = await ServerctlService.Start()
    opMsg.value = `启动成功，pid=${pid}`
    pushLog('INFO', 'beastserver 启动成功', {pid})
    await refreshStatus()
  } catch (e: any) {
    opMsg.value = `启动失败：${e?.message ?? e}`
    pushLog('ERROR', '启动失败', {err: String(e?.message ?? e)})
  } finally {
    startRunning.value = false
  }
}

async function stop() {
  stopRunning.value = true
  opMsg.value = ''
  try {
    await ensureCredsApplied()
    await ServerctlService.Stop()
    opMsg.value = '已停止'
    pushLog('INFO', 'beastserver 已停止')
    await refreshStatus()
  } catch (e: any) {
    opMsg.value = `停止失败：${e?.message ?? e}`
    pushLog('ERROR', '停止失败', {err: String(e?.message ?? e)})
  } finally {
    stopRunning.value = false
  }
}

async function restart() {
  restartRunning.value = true
  opMsg.value = ''
  try {
    await ensureCredsApplied()
    const pid = await ServerctlService.Restart()
    opMsg.value = `重启成功，pid=${pid}`
    pushLog('INFO', 'beastserver 重启成功', {pid})
    await refreshStatus()
  } catch (e: any) {
    opMsg.value = `重启失败：${e?.message ?? e}`
    pushLog('ERROR', '重启失败', {err: String(e?.message ?? e)})
  } finally {
    restartRunning.value = false
  }
}

// ============ 日志 tail（状态挂在 serverctl store，切页面不销毁） ============
const serverctlStore = useServerctlStore()
const logLines = computed({
  get: () => serverctlStore.logLines,
  set: (v: number) => { serverctlStore.logLines = v },
})
const logRunning = computed(() => serverctlStore.running)
const logErr = computed(() => serverctlStore.lastError)
const autoRefresh = computed(() => serverctlStore.autoRefresh)

async function refreshLogs() {
  await ensureCredsApplied()
  await serverctlStore.refreshOnce()
}

function toggleAutoRefresh(v: boolean) {
  serverctlStore.toggleAutoRefresh(v)
}

// ============ 配置管理（server.json 整文件编辑） ============
const configContent = ref('')
const configOriginalContent = ref('')  // 用于 dirty 比较
const configRunning = ref(false)
const configErr = ref('')
const configMsg = ref('')
const configDirty = computed(() => configContent.value !== configOriginalContent.value)
const configNeedsRestart = ref(false)  // 保存成功后提示

async function loadConfig() {
  configRunning.value = true
  configErr.value = ''
  configMsg.value = ''
  try {
    await ensureCredsApplied()
    const content = await ServerctlService.ReadConfig()
    configContent.value = content
    configOriginalContent.value = content
    configNeedsRestart.value = false
    configMsg.value = `已加载 ${content.length} 字节`
    pushLog('INFO', '配置已加载', {bytes: content.length})
  } catch (e: any) {
    configErr.value = `${e?.message ?? e}`
    pushLog('ERROR', '加载配置失败', {err: String(e?.message ?? e)})
  } finally {
    configRunning.value = false
  }
}

function formatConfig() {
  try {
    const obj = JSON.parse(configContent.value)
    configContent.value = JSON.stringify(obj, null, 2)
    pushLog('INFO', 'JSON 已格式化')
  } catch (e: any) {
    configErr.value = `格式化失败：${e?.message ?? e}`
    pushLog('WARN', 'JSON 格式化失败', {err: String(e?.message ?? e)})
  }
}

async function saveConfig() {
  // 先校验 JSON 合法性，避免上传坏配置导致 beastserver 启动失败
  try {
    JSON.parse(configContent.value)
  } catch (e: any) {
    configErr.value = `JSON 语法错误，未上传：${e?.message ?? e}`
    return
  }
  configRunning.value = true
  configErr.value = ''
  configMsg.value = ''
  try {
    await ensureCredsApplied()
    await ServerctlService.WriteConfig(configContent.value)
    configOriginalContent.value = configContent.value
    configNeedsRestart.value = true
    configMsg.value = '配置已上传，需重启 beastserver 使其生效'
    pushLog('INFO', '配置已保存到远端', {bytes: configContent.value.length})
  } catch (e: any) {
    configErr.value = `${e?.message ?? e}`
    pushLog('ERROR', '保存配置失败', {err: String(e?.message ?? e)})
  } finally {
    configRunning.value = false
  }
}

// ============ 建房（gRPC RoomService.CreateRoom） ============
const engineName = ref('pixelmoba')
const instanceIdInput = ref('')
const playerIdsStr = ref('')
const createRunning = ref(false)
const createMsg = ref('')
const createErr = ref('')
const lastInstanceId = ref('')
const lastEngineName = ref('')

async function createRoom() {
  createRunning.value = true
  createMsg.value = ''
  createErr.value = ''
  try {
    await ensureCredsApplied()
    // 解析 player_ids（逗号分隔，trim 空值）
    const players = playerIdsStr.value
      .split(',')
      .map(s => s.trim())
      .filter(s => s.length > 0)
    const result = await RoomctlService.CreateRoom(
      engineName.value,
      players,
      instanceIdInput.value,
    )
    if (result) {
      lastInstanceId.value = result.instanceID ?? ''
      lastEngineName.value = result.engineName ?? ''
      createMsg.value = `建房成功：instance_id=${lastInstanceId.value}`
      pushLog('INFO', 'CreateRoom 成功', {
        engine: lastEngineName.value,
        instance_id: lastInstanceId.value,
        players: players.length,
      })
    } else {
      createErr.value = 'CreateRoom 返回 null'
      pushLog('ERROR', 'CreateRoom 返回 null')
    }
  } catch (e: any) {
    createErr.value = `${e?.message ?? e}`
    pushLog('ERROR', '建房失败', {err: String(e?.message ?? e)})
  } finally {
    createRunning.value = false
  }
}

function copyInstanceId() {
  if (!lastInstanceId.value) return
  navigator.clipboard.writeText(lastInstanceId.value).then(() => {
    pushLog('INFO', 'instance_id 已复制到剪贴板', {instance_id: lastInstanceId.value})
  }).catch(() => {
    // 退化方案：选中 input
    const el = document.getElementById('last-instance-id') as HTMLInputElement | null
    el?.select()
  })
}

// ============ lifecycle ============
onMounted(async () => {
  // 加载第一个 profile 的凭据，但不自动 apply（密码可能为空，需要用户填）
  loadFromProfile()
  // 如果当前 profile 已经有 host（说明之前填过），自动应用
  if (host.value) {
    await applyCreds()
    await refreshLogs()
  }
})
</script>

<template>
  <div class="server-control">
    <header class="page-header">
      <h1>BeastServer 进程控制</h1>
      <p class="page-sub">通过 SSH 远程启停 beastserver，日志会推送到下方 <code>serverctl</code> 标签页</p>
    </header>

    <!-- 凭据区 -->
    <section class="gw-card">
      <div class="gw-card-title">SSH 凭据</div>

      <!-- Profile 选择器 -->
      <div class="profile-row">
        <label class="profile-select-label">Profile
          <select
            class="profile-select"
            :value="profiles.currentId ?? ''"
            @change="onPickProfile"
            title="选择服务器 profile"
          >
            <option v-for="p in profiles.profiles" :key="p.id" :value="p.id">
              {{ p.name }}{{ dirty && p.id === profiles.currentId ? ' *' : '' }}
            </option>
          </select>
        </label>
        <button class="gw-btn-secondary" @click="newProfile" title="新建 profile">+ 新建</button>
        <button class="gw-btn-danger" @click="deleteProfile" title="删除当前 profile">🗑 删除</button>
        <span v-if="dirty" class="dirty-hint">未保存（点"应用凭据"自动保存）</span>
      </div>

      <div class="form-row">
        <label>Host
          <input v-model="host" type="text" placeholder="192.168.217.130" @input="markDirty"/>
        </label>
        <label>Port
          <input v-model.number="port" type="number" min="1" max="65535" @input="markDirty"/>
        </label>
        <label>User
          <input v-model="user" type="text" placeholder="用户名" @input="markDirty"/>
        </label>
        <label>Password
          <input v-model="password" type="password" placeholder="密码" @input="markDirty"/>
        </label>
      </div>
      <div class="action-row">
        <button :disabled="applyCredsRunning" @click="applyCreds">
          {{ applyCredsRunning ? '应用中...' : '应用凭据' }}
        </button>
        <span v-if="applyCredsMsg" class="msg">{{ applyCredsMsg }}</span>
      </div>
    </section>

    <!-- 状态 + 控制按钮 -->
    <section class="gw-card">
      <div class="gw-card-title">进程状态</div>
      <div class="status-row">
        <span class="status-dot" :style="{backgroundColor: statusDotColor, boxShadow: `0 0 8px ${statusDotColor}`}"></span>
        <span class="status-text">{{ statusText }}</span>
        <button class="gw-btn-secondary" :disabled="statusRunning" @click="refreshStatus">
          {{ statusRunning ? '查询中...' : '刷新状态' }}
        </button>
      </div>
      <div v-if="statusErr" class="err">{{ statusErr }}</div>
      <div class="action-row">
        <button class="gw-btn-success" :disabled="!canStart" @click="start">
          {{ startRunning ? '启动中...' : '启动' }}
        </button>
        <button class="gw-btn-danger" :disabled="!canStop" @click="stop">
          {{ stopRunning ? '停止中...' : '停止' }}
        </button>
        <button :disabled="!canRestart" @click="restart">
          {{ restartRunning ? '重启中...' : '重启' }}
        </button>
        <span v-if="opMsg" class="msg">{{ opMsg }}</span>
      </div>
    </section>

    <!-- 建房（gRPC RoomService.CreateRoom） -->
    <section class="gw-card">
      <div class="gw-card-title">建房（gRPC RoomService.CreateRoom）</div>
      <p class="hint-text">
        通过 gRPC 调 beastserver <code>{{ grpcPort }}</code> 端口的 RoomService.CreateRoom 创建房间实例。
        host 跟随 SSH 凭据里的 host，grpcPort 跟随当前 profile。
        日志会推送到下方 <code>roomctl</code> 标签页。
      </p>

      <div class="form-row">
        <label>gRPC Port
          <input v-model.number="grpcPort" type="number" min="1" max="65535" @input="markDirty"/>
        </label>
        <label>engine_name
          <input v-model="engineName" type="text" placeholder="pixelmoba"/>
        </label>
        <label>instance_id（可空）
          <input v-model="instanceIdInput" type="text" placeholder="留空由平台生成"/>
        </label>
      </div>
      <div class="form-row">
        <label>player_ids（逗号分隔，可空）
          <input v-model="playerIdsStr" type="text" placeholder="p1,p2"/>
        </label>
      </div>

      <div class="action-row">
        <button :disabled="createRunning" @click="createRoom">
          {{ createRunning ? '建房中...' : '建房' }}
        </button>
        <span v-if="createMsg" class="msg msg-success">{{ createMsg }}</span>
        <span v-if="createErr" class="msg msg-error">{{ createErr }}</span>
      </div>

      <div v-if="lastInstanceId" class="instance-result">
        <label>instance_id（最近一次）
          <div class="instance-id-row">
            <input id="last-instance-id" :value="lastInstanceId" readonly/>
            <button class="gw-btn-secondary" @click="copyInstanceId" title="复制到剪贴板">复制</button>
          </div>
        </label>
      </div>
    </section>

    <!-- 配置管理（远端 server.json 整文件编辑） -->
    <section class="gw-card">
      <div class="gw-card-title">配置管理（server.json）</div>
      <p class="hint-text">
        通过 SSH cat 拉取远端 <code>beastserver/config/server.json</code>，编辑后保存会覆盖远端文件（保存前自动备份为 <code>.bak</code>）。
        保存成功后需重启 beastserver 使配置生效。
      </p>
      <div class="action-row config-toolbar">
        <button class="gw-btn-secondary" :disabled="configRunning" @click="loadConfig">
          {{ configRunning ? '加载中...' : '加载' }}
        </button>
        <button class="gw-btn-secondary" :disabled="!configContent" @click="formatConfig">格式化</button>
        <button :disabled="!configDirty || configRunning" @click="saveConfig">
          {{ configRunning ? '保存中...' : '保存' }}
        </button>
        <span v-if="configDirty" class="dirty-hint">未保存</span>
        <span v-if="configMsg" class="msg">{{ configMsg }}</span>
        <span v-if="configErr" class="msg msg-error">{{ configErr }}</span>
      </div>
      <div v-if="configNeedsRestart" class="restart-hint">
        <span>配置已上传，需重启 beastserver 使其生效</span>
        <button :disabled="!canRestart" @click="restart">
          {{ restartRunning ? '重启中...' : '重启 beastserver' }}
        </button>
      </div>
      <textarea
        v-model="configContent"
        class="config-editor"
        spellcheck="false"
        placeholder="点「加载」拉取远端 server.json..."
      ></textarea>
    </section>

    <!-- 日志 tail（拉取到 LogWindow 的 serverctl tab） -->
    <section class="gw-card">
      <div class="gw-card-title">服务端原始日志 tail</div>
      <p class="hint-text">
        点击"拉取日志"会把远端 beastserver 日志文件的内容推送到下方
        <code>serverctl</code> 标签页。
      </p>
      <div class="action-row">
        <label>行数
          <input v-model.number="logLines" type="number" min="1" max="10000"/>
        </label>
        <button class="gw-btn-secondary" :disabled="logRunning" @click="refreshLogs">
          {{ logRunning ? '拉取中...' : '拉取日志' }}
        </button>
        <label class="auto-refresh">
          <input type="checkbox" :checked="autoRefresh"
                 @change="toggleAutoRefresh(($event.target as HTMLInputElement).checked)"/>
          自动拉取 (2s)
        </label>
      </div>
      <div v-if="logErr" class="err">{{ logErr }}</div>
    </section>
  </div>
</template>

<style scoped>
.server-control {
  padding: 16px 24px;
  max-width: 900px;
  margin: 0 auto;
}

.page-header {
  margin-bottom: 16px;
}
.page-header h1 {
  font-family: var(--font-display);
  font-size: 18px;
  font-weight: 700;
  letter-spacing: 0.12em;
  color: var(--border-strong);
  text-transform: uppercase;
  margin-bottom: 4px;
}
.page-sub {
  font-size: 12px;
  color: var(--text-muted);
  margin: 0;
}
.page-sub code {
  font-family: var(--font-mono);
  color: var(--border-strong);
  background: var(--bg-inset);
  padding: 0 4px;
  border-radius: 3px;
}

/* Profile 选择器行 */
.profile-row {
  display: flex;
  align-items: flex-end;
  gap: 12px;
  margin-bottom: 14px;
  padding-bottom: 12px;
  border-bottom: 1px dashed var(--border-soft);
}
.profile-select-label {
  font-size: 12px;
  color: var(--text-muted);
  flex: 0 0 220px;
}
.profile-select {
  /* 用全局 height: 32px（来自 style.css） */
  width: 100%;
  margin-top: 4px;
}
.dirty-hint {
  font-size: 11px;
  color: var(--warn);
  font-style: italic;
}

.form-row {
  display: flex;
  flex-wrap: wrap;
  gap: 12px;
  margin-bottom: 10px;
}
.form-row label {
  display: flex;
  flex-direction: column;
  gap: 4px;
  font-size: 12px;
  color: var(--text-muted);
}
.form-row input {
  width: 180px;
}

.action-row {
  display: flex;
  align-items: center;
  gap: 12px;
  flex-wrap: wrap;
}
.action-row label {
  display: flex;
  align-items: center;
  gap: 6px;
  font-size: 12px;
  color: var(--text-muted);
}
.action-row input[type="number"] {
  width: 80px;
}

.msg {
  color: var(--text-muted);
  font-size: 12px;
}
.err {
  color: var(--error);
  font-size: 12px;
  margin: 4px 0;
}

.status-row {
  display: flex;
  align-items: center;
  gap: 10px;
  margin-bottom: 10px;
}
.status-dot {
  display: inline-block;
  width: 10px;
  height: 10px;
  border-radius: 50%;
}
.status-text {
  font-family: var(--font-mono);
  font-size: 13px;
  color: var(--text-main);
  flex: 1;
}

.auto-refresh {
  cursor: pointer;
}

.hint-text {
  font-size: 12px;
  color: var(--text-muted);
  margin: 0 0 10px;
  line-height: 1.5;
}
.hint-text code {
  font-family: var(--font-mono);
  color: var(--border-strong);
  background: var(--bg-inset);
  padding: 0 4px;
  border-radius: 3px;
}

/* 建房卡片样式 */
.msg-success {
  color: var(--success) !important;
}
.msg-error {
  color: var(--error) !important;
}
.instance-result {
  margin-top: 12px;
  padding-top: 10px;
  border-top: 1px dashed var(--border-soft);
}
.instance-id-row {
  display: flex;
  gap: 8px;
  align-items: stretch;
  margin-top: 4px;
}
.instance-id-row input {
  flex: 1;
  font-family: var(--font-mono);
  color: var(--border-strong);
  background: var(--bg-inset);
}
.instance-id-row button {
  flex: 0 0 auto;
}

/* 配置管理卡片 */
.config-toolbar {
  margin-bottom: 8px;
}
.config-editor {
  width: 100%;
  min-height: 320px;
  padding: 8px 10px;
  font-family: var(--font-mono);
  font-size: 12px;
  line-height: 1.5;
  background: var(--bg-inset);
  color: var(--text-main);
  border: 1px solid var(--border-soft);
  border-radius: var(--radius-sm);
  resize: vertical;
  white-space: pre;
  overflow: auto;
}
.config-editor:focus {
  outline: none;
  border-color: var(--accent-strong);
  box-shadow: 0 0 0 2px var(--accent-glow);
}
.restart-hint {
  display: flex;
  align-items: center;
  gap: 12px;
  padding: 8px 12px;
  margin: 8px 0;
  background: var(--accent-glow);
  border: 1px solid var(--accent-strong);
  border-radius: var(--radius-sm);
  font-size: 12px;
  color: var(--accent-strong);
}
.restart-hint button {
  padding: 4px 12px;
  font-size: 12px;
  background: var(--accent-strong);
  color: var(--bg-primary);
  border: none;
  border-radius: var(--radius-sm);
  cursor: pointer;
}
.restart-hint button:hover:not(:disabled) {
  opacity: 0.85;
}
.restart-hint button:disabled {
  opacity: 0.4;
  cursor: not-allowed;
}
</style>
