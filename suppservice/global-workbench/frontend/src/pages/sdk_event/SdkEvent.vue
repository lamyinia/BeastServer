<script lang="ts" setup>
import {ref, computed, onMounted, onUnmounted} from 'vue'
import * as SdkEventService from '../../../bindings/global-workbench/sdkeventservice.js'
import {useLogsStore} from '../../stores/logs'

const logs = useLogsStore()

function pushLog(level: 'INFO' | 'WARN' | 'ERROR', msg: string, fields?: Record<string, unknown>) {
  logs.pushEntry({tag: 'target.go', level, msg, ts: Date.now(), fields})
  logs.setActiveTag('target.go')
}

// ============ 链路列表 ============
type LinkInfo = {
  id: string
  host: string
  port: number
  transport: string
  state: string
}

const links = ref<LinkInfo[]>([])
const activeLinkID = ref<string>('')
const activeLink = computed(() => links.value.find(l => l.id === activeLinkID.value) ?? null)

async function refreshLinks() {
  try {
    const list = await SdkEventService.ListLinks()
    links.value = list ?? []
    // active 不在列表里（被关掉了）→ 清空或选第一个
    if (activeLinkID.value && !links.value.find(l => l.id === activeLinkID.value)) {
      activeLinkID.value = links.value[0]?.id ?? ''
    } else if (!activeLinkID.value && links.value.length > 0) {
      activeLinkID.value = links.value[0].id
    }
  } catch (e: any) {
    pushLog('ERROR', 'ListLinks 失败', {err: String(e?.message ?? e)})
  }
}

// ============ 新建链路表单 ============
type TransportOpt = 'tcp' | 'tls' | 'kcp' | 'websocket'
const newID = ref('')
const newHost = ref('192.168.217.130')
const newPort = ref(8010)
const newTransport = ref<TransportOpt>('tcp')
const newTLS = ref({
  caPath: 'd:/git-project/BeastServer-project/beastserver/config/certs/ca/ca_cert.pem',
  serverName: '',
  certPath: '',
  keyPath: '',
})
const transportOptions: {value: TransportOpt, label: string, disabled: boolean}[] = [
  {value: 'tcp',       label: 'TCP',       disabled: false},
  {value: 'tls',       label: 'TLS',       disabled: false},
  {value: 'kcp',       label: 'KCP (v3)',  disabled: true},
  {value: 'websocket', label: 'WebSocket (v4)', disabled: true},
]
const newLinkRunning = ref(false)

async function createLink() {
  if (!newID.value.trim()) {
    pushLog('ERROR', 'linkID 必填')
    return
  }
  newLinkRunning.value = true
  try {
    const params: {
      host: string
      port: number
      transport: TransportOpt
      tls: {caPath: string; serverName: string; certPath: string; keyPath: string} | null
    } = {
      host: newHost.value,
      port: newPort.value,
      transport: newTransport.value,
      tls: null,
    }
    if (newTransport.value === 'tls') {
      if (!newTLS.value.caPath) {
        throw new Error('TLS 模式必须填写 CA Path')
      }
      params.tls = {...newTLS.value}
    }
    await SdkEventService.CreateLink(newID.value.trim(), params as any)
    pushLog('INFO', `${newID.value} 创建成功`, {transport: newTransport.value})
    activeLinkID.value = newID.value.trim()
    newID.value = ''
    await refreshLinks()
  } catch (e: any) {
    pushLog('ERROR', 'CreateLink 失败', {err: String(e?.message ?? e)})
  } finally {
    newLinkRunning.value = false
  }
}

async function closeLink(id: string) {
  try {
    await SdkEventService.CloseLink(id)
    pushLog('INFO', `${id} 关闭`)
    if (activeLinkID.value === id) {
      activeLinkID.value = ''
    }
    await refreshLinks()
  } catch (e: any) {
    pushLog('ERROR', 'CloseLink 失败', {err: String(e?.message ?? e)})
  }
}

// ============ Login ============
const loginToken = ref('dev:1')
const loginDeviceID = ref('workbench')
const loginVersion = ref('v0.1')
const loginRunning = ref(false)

async function login() {
  if (!activeLinkID.value) return
  loginRunning.value = true
  try {
    await SdkEventService.LoginLink(activeLinkID.value, loginToken.value, loginDeviceID.value, loginVersion.value)
    pushLog('INFO', `${activeLinkID.value} login ok`, {token: loginToken.value})
    await refreshLinks()
  } catch (e: any) {
    pushLog('ERROR', 'LoginLink 失败', {err: String(e?.message ?? e)})
  } finally {
    loginRunning.value = false
  }
}

// ============ 5 个 Send ============
const defaultTimeoutMs = 5000

// 1) Echo
const echoInput = ref('hello sdk_event')
const echoResult = ref('')
const echoRunning = ref(false)
async function sendEcho() {
  if (!activeLinkID.value) return
  echoRunning.value = true
  echoResult.value = ''
  try {
    const r = await SdkEventService.SendEcho(activeLinkID.value, echoInput.value, defaultTimeoutMs)
    echoResult.value = r?.text ?? ''
    pushLog('INFO', `${activeLinkID.value} Echo ok`, {text: echoResult.value})
  } catch (e: any) {
    echoResult.value = `错误：${e?.message ?? e}`
    pushLog('ERROR', 'SendEcho 失败', {err: String(e?.message ?? e)})
  } finally {
    echoRunning.value = false
  }
}

// 2) SeqEcho
const seqInput = ref('seq-test')
const seqClientSeq = ref(42)
const seqResult = ref('')
const seqRunning = ref(false)
async function sendSeqEcho() {
  if (!activeLinkID.value) return
  seqRunning.value = true
  seqResult.value = ''
  try {
    const r = await SdkEventService.SendSeqEcho(activeLinkID.value, seqInput.value, seqClientSeq.value, defaultTimeoutMs)
    if (r) {
      seqResult.value = `clientSeq=${r.clientSeq} text="${r.text}"`
      pushLog('INFO', `${activeLinkID.value} SeqEcho ok`, {clientSeq: r.clientSeq, text: r.text})
    } else {
      seqResult.value = '返回 null'
    }
  } catch (e: any) {
    seqResult.value = `错误：${e?.message ?? e}`
    pushLog('ERROR', 'SendSeqEcho 失败', {err: String(e?.message ?? e)})
  } finally {
    seqRunning.value = false
  }
}

// 3) BytesEcho
const bytesInput = ref('00ff0a0d01fe')
const bytesResult = ref('')
const bytesRunning = ref(false)
async function sendBytesEcho() {
  if (!activeLinkID.value) return
  bytesRunning.value = true
  bytesResult.value = ''
  try {
    const r = await SdkEventService.SendBytesEcho(activeLinkID.value, bytesInput.value, defaultTimeoutMs)
    if (r) {
      bytesResult.value = `len=${r.len} hex=${r.hexPayload.slice(0, 64)}${r.hexPayload.length > 64 ? '...' : ''}`
      pushLog('INFO', `${activeLinkID.value} BytesEcho ok`, {len: r.len})
    } else {
      bytesResult.value = '返回 null'
    }
  } catch (e: any) {
    bytesResult.value = `错误：${e?.message ?? e}`
    pushLog('ERROR', 'SendBytesEcho 失败', {err: String(e?.message ?? e)})
  } finally {
    bytesRunning.value = false
  }
}

// 4) BigEcho
const bigSize = ref(1024)
const bigResult = ref('')
const bigRunning = ref(false)
async function sendBigEcho() {
  if (!activeLinkID.value) return
  bigRunning.value = true
  bigResult.value = ''
  try {
    const r = await SdkEventService.SendBigEcho(activeLinkID.value, bigSize.value, 15000)
    if (r) {
      const verified = r.verified ? '✓ 完整性校验通过' : `✗ 校验失败：${r.verifyErr}`
      bigResult.value = `size=${r.size} hexPreview=${r.hexPreview} | ${verified}`
      pushLog('INFO', `${activeLinkID.value} BigEcho ok`, {size: r.size, verified: r.verified})
    } else {
      bigResult.value = '返回 null'
    }
  } catch (e: any) {
    bigResult.value = `错误：${e?.message ?? e}`
    pushLog('ERROR', 'SendBigEcho 失败', {err: String(e?.message ?? e)})
  } finally {
    bigRunning.value = false
  }
}

// 5) TransportInfo
const transportPref = ref('')
const transportResult = ref('')
const transportRunning = ref(false)
async function sendTransportInfo() {
  if (!activeLinkID.value) return
  transportRunning.value = true
  transportResult.value = ''
  try {
    const r = await SdkEventService.SendTransportInfo(activeLinkID.value, transportPref.value, defaultTimeoutMs)
    if (r) {
      transportResult.value = `preference="${r.requestedPreference}" playerId="${r.playerId}"`
      pushLog('INFO', `${activeLinkID.value} TransportInfo ok`, {preference: r.requestedPreference, playerId: r.playerId})
    } else {
      transportResult.value = '返回 null'
    }
  } catch (e: any) {
    transportResult.value = `错误：${e?.message ?? e}`
    pushLog('ERROR', 'SendTransportInfo 失败', {err: String(e?.message ?? e)})
  } finally {
    transportRunning.value = false
  }
}

// ============ 状态颜色 ============
function stateColor(state: string): string {
  switch (state) {
    case 'AUTHED':     return 'var(--success)'
    case 'CONNECTED':
    case 'AUTHING':
    case 'CONNECTING': return 'var(--warning)'
    case 'DISCONNECTED': return 'var(--error)'
    default:           return 'var(--text-faint)'
  }
}

const canLogin = computed(() =>
    !!activeLink.value &&
    !loginRunning.value &&
    activeLink.value.state === 'CONNECTED')
const canSend = computed(() =>
    !!activeLink.value &&
    activeLink.value.state === 'AUTHED')

// ============ lifecycle ============
let pollTimer: number | null = null

onMounted(async () => {
  await refreshLinks()
  // 1s 轮询：足够及时显示状态变化，又不至于太频繁
  pollTimer = window.setInterval(refreshLinks, 1000)
})

onUnmounted(() => {
  if (pollTimer !== null) {
    window.clearInterval(pollTimer)
  }
})
</script>

<template>
  <div class="sdk-event">
    <header class="page-header">
      <h1>go-sdk 自检</h1>
      <p class="page-sub">
        客户端 Go SDK 多链路自检：可同时建 TCP / TLS / KCP / WS 多条 BeastClient 并行收发。
        前置：beastserver 已启动（<router-link to="/server/control">进程控制</router-link>），
        且已建过房（CreateRoom）使建房者 Session 自动 bind 到 instance。
      </p>
    </header>

    <div class="sdk-event-layout">
      <!-- 左侧：新建 + 链路列表 -->
      <aside class="link-sidebar">
        <!-- 新建链路表单 -->
        <div class="sidebar-section">
          <div class="sidebar-title">+ 新建链路</div>
          <div class="new-link-form">
            <label class="field">
              <span>LinkID</span>
              <input v-model="newID" type="text" placeholder="如 tcp-1 / tls-1" @keyup.enter="createLink"/>
            </label>
            <div class="form-row-inline">
              <label class="field grow">
                <span>Host</span>
                <input v-model="newHost" type="text"/>
              </label>
              <label class="field">
                <span>Port</span>
                <input v-model.number="newPort" type="number" min="1" max="65535"/>
              </label>
            </div>
            <label class="field">
              <span>Transport</span>
              <select v-model="newTransport">
                <option
                  v-for="opt in transportOptions"
                  :key="opt.value"
                  :value="opt.value"
                  :disabled="opt.disabled"
                >
                  {{ opt.label }}
                </option>
              </select>
            </label>
            <div v-if="newTransport === 'tls'" class="tls-form">
              <label class="field">
                <span>CA Path</span>
                <input v-model="newTLS.caPath" type="text" placeholder="ca_cert.pem 路径"/>
              </label>
              <label class="field">
                <span>ServerName (空=Host)</span>
                <input v-model="newTLS.serverName" type="text"/>
              </label>
              <div class="form-row-inline">
                <label class="field grow">
                  <span>Cert Path (可空)</span>
                  <input v-model="newTLS.certPath" type="text"/>
                </label>
                <label class="field grow">
                  <span>Key Path (可空)</span>
                  <input v-model="newTLS.keyPath" type="text"/>
                </label>
              </div>
            </div>
            <button class="gw-btn-success" :disabled="newLinkRunning" @click="createLink">
              {{ newLinkRunning ? '创建中...' : '创建链路' }}
            </button>
          </div>
        </div>

        <!-- 链路列表 -->
        <div class="sidebar-section">
          <div class="sidebar-title">链路列表 ({{ links.length }})</div>
          <ul class="link-list">
            <li
              v-for="link in links"
              :key="link.id"
              :class="{active: link.id === activeLinkID}"
              @click="activeLinkID = link.id"
            >
              <span class="dot" :style="{backgroundColor: stateColor(link.state), boxShadow: `0 0 6px ${stateColor(link.state)}`}"></span>
              <div class="link-info">
                <div class="link-id">{{ link.id }}</div>
                <div class="link-meta">{{ link.transport.toUpperCase() }} · {{ link.host }}:{{ link.port }}</div>
                <div class="link-state" :style="{color: stateColor(link.state)}">{{ link.state }}</div>
              </div>
              <button class="close-btn" title="关闭链路" @click.stop="closeLink(link.id)">×</button>
            </li>
            <li v-if="links.length === 0" class="empty">无链路，先在上面新建</li>
          </ul>
        </div>
      </aside>

      <!-- 右侧：详情面板 -->
      <main class="link-detail">
        <div v-if="activeLink" class="detail-content">
          <!-- 详情头部 -->
          <header class="detail-header">
            <h2>{{ activeLink.id }}</h2>
            <span class="state-tag" :style="{color: stateColor(activeLink.state), borderColor: stateColor(activeLink.state)}">
              {{ activeLink.state }}
            </span>
            <span class="transport-tag">{{ activeLink.transport.toUpperCase() }}</span>
            <span class="addr">{{ activeLink.host }}:{{ activeLink.port }}</span>
            <button class="gw-btn-secondary" @click="refreshLinks">刷新</button>
          </header>

          <!-- Login 卡片 -->
          <section class="gw-card">
            <div class="gw-card-title">Login</div>
            <p class="hint-text">Connect 成功后 state=CONNECTED，Login 后 state=AUTHED 才能发 Send。</p>
            <div class="form-row">
              <label class="grow">
                <span>Token</span>
                <input v-model="loginToken" type="text"/>
              </label>
              <label>
                <span>DeviceID</span>
                <input v-model="loginDeviceID" type="text"/>
              </label>
              <label>
                <span>Version</span>
                <input v-model="loginVersion" type="text"/>
              </label>
            </div>
            <div class="action-row">
              <button :disabled="!canLogin" @click="login">
                {{ loginRunning ? 'Login 中...' : 'Login' }}
              </button>
            </div>
          </section>

          <!-- 1) Echo -->
          <section class="gw-card">
            <div class="gw-card-title">1) Echo <code>sdk.echo</code></div>
            <p class="hint-text">string 回显，验证 codec 编解码对称。route: <code>sdk.echo</code> → <code>sdk.echo.resp</code></p>
            <div class="form-row">
              <label class="grow">
                <span>Text</span>
                <input v-model="echoInput" type="text" @keyup.enter="sendEcho"/>
              </label>
            </div>
            <div class="action-row">
              <button :disabled="!canSend || echoRunning" @click="sendEcho">
                {{ echoRunning ? '发送中...' : '发送' }}
              </button>
              <span v-if="echoResult" class="msg">响应：{{ echoResult }}</span>
            </div>
          </section>

          <!-- 2) SeqEcho -->
          <section class="gw-card">
            <div class="gw-card-title">2) SeqEcho <code>sdk.echo.seq</code></div>
            <p class="hint-text">client_seq 透传，验证帧头往返。route: <code>sdk.echo.seq</code> → <code>sdk.echo.seq.resp</code></p>
            <div class="form-row">
              <label class="grow">
                <span>Text</span>
                <input v-model="seqInput" type="text" @keyup.enter="sendSeqEcho"/>
              </label>
              <label>
                <span>ClientSeq</span>
                <input v-model.number="seqClientSeq" type="number" min="0"/>
              </label>
            </div>
            <div class="action-row">
              <button :disabled="!canSend || seqRunning" @click="sendSeqEcho">
                {{ seqRunning ? '发送中...' : '发送' }}
              </button>
              <span v-if="seqResult" class="msg">响应：{{ seqResult }}</span>
            </div>
          </section>

          <!-- 3) BytesEcho -->
          <section class="gw-card">
            <div class="gw-card-title">3) BytesEcho <code>sdk.echo.bytes</code></div>
            <p class="hint-text">bytes 二进制透传（含 0x00 / 0xFF 等），验证 codec bytes 字段处理。</p>
            <div class="form-row">
              <label class="grow">
                <span>Hex Payload</span>
                <input v-model="bytesInput" type="text" placeholder="00ff0a0d01fe" @keyup.enter="sendBytesEcho"/>
              </label>
            </div>
            <div class="action-row">
              <button :disabled="!canSend || bytesRunning" @click="sendBytesEcho">
                {{ bytesRunning ? '发送中...' : '发送' }}
              </button>
              <span v-if="bytesResult" class="msg">响应：{{ bytesResult }}</span>
            </div>
          </section>

          <!-- 4) BigEcho -->
          <section class="gw-card">
            <div class="gw-card-title">4) BigEcho <code>sdk.echo.big</code></div>
            <p class="hint-text">大消息 echo（byte[i] = i &amp; 0xFF，clamp 60KB），验证大消息编解码 + 完整性。</p>
            <div class="form-row">
              <label class="grow">
                <span>Size (bytes)</span>
                <input v-model.number="bigSize" type="number" min="1" max="61440"/>
              </label>
            </div>
            <div class="action-row">
              <button :disabled="!canSend || bigRunning" @click="sendBigEcho">
                {{ bigRunning ? '发送中...' : '发送' }}
              </button>
              <span v-if="bigResult" class="msg">响应：{{ bigResult }}</span>
            </div>
          </section>

          <!-- 5) TransportInfo -->
          <section class="gw-card">
            <div class="gw-card-title">5) TransportInfo <code>sdk.transport.info</code></div>
            <p class="hint-text">transport 调度测试。v1 单 channel：preference 留空。v3+ 多 channel：preference="prefer_kcp"。</p>
            <div class="form-row">
              <label class="grow">
                <span>Preference</span>
                <input v-model="transportPref" type="text" placeholder="留空 = Any" @keyup.enter="sendTransportInfo"/>
              </label>
            </div>
            <div class="action-row">
              <button :disabled="!canSend || transportRunning" @click="sendTransportInfo">
                {{ transportRunning ? '发送中...' : '发送' }}
              </button>
              <span v-if="transportResult" class="msg">响应：{{ transportResult }}</span>
            </div>
          </section>
        </div>

        <!-- 未选中链路 -->
        <div v-else class="empty-detail">
          <div class="empty-icon">◇</div>
          <p>选择左侧链路，或新建一条链路开始联调</p>
        </div>
      </main>
    </div>
  </div>
</template>

<style scoped>
.sdk-event {
  display: flex;
  flex-direction: column;
  gap: 16px;
  height: 100%;
}

.page-header h1 {
  font-family: var(--font-display);
  font-size: 22px;
  font-weight: 700;
  color: var(--text-main);
  letter-spacing: 0.04em;
  margin: 0 0 4px;
}
.page-sub {
  font-size: 12px;
  color: var(--text-muted);
  margin: 0;
  line-height: 1.5;
}
.page-sub code {
  font-family: var(--font-mono);
  font-size: 11px;
  padding: 1px 6px;
  background: var(--bg-elevated);
  border-radius: var(--radius-sm);
  color: var(--accent-strong);
}
.page-sub a {
  color: var(--accent-strong);
  text-decoration: underline;
}

/* === 双栏布局 === */
.sdk-event-layout {
  display: flex;
  gap: 16px;
  flex: 1;
  min-height: 0;
}

/* === 左侧 === */
.link-sidebar {
  width: 320px;
  flex-shrink: 0;
  display: flex;
  flex-direction: column;
  gap: 12px;
  overflow-y: auto;
}
.sidebar-section {
  background: var(--bg-elevated);
  border: 1px solid var(--border-soft);
  border-radius: var(--radius-md);
  padding: 12px;
}
.sidebar-title {
  font-family: var(--font-display);
  font-size: 12px;
  letter-spacing: 0.08em;
  text-transform: uppercase;
  color: var(--text-muted);
  margin-bottom: 10px;
}

/* 新建表单 */
.new-link-form {
  display: flex;
  flex-direction: column;
  gap: 8px;
}
.field {
  display: flex;
  flex-direction: column;
  gap: 4px;
  font-size: 11px;
  color: var(--text-muted);
  letter-spacing: 0.04em;
  text-transform: uppercase;
}
.field span {
  font-size: 10px;
}
.field input,
.field select {
  padding: 5px 8px;
  font-size: 12px;
  font-family: var(--font-mono);
  background: var(--bg-primary);
  border: 1px solid var(--border-soft);
  border-radius: var(--radius-sm);
  color: var(--text-main);
  min-width: 0;
}
.field input:focus,
.field select:focus {
  outline: none;
  border-color: var(--accent-strong);
  box-shadow: 0 0 0 2px var(--accent-glow);
}
.form-row-inline {
  display: flex;
  gap: 8px;
}
.form-row-inline .field.grow {
  flex: 1;
}
.tls-form {
  display: flex;
  flex-direction: column;
  gap: 6px;
  padding: 8px;
  background: var(--bg-primary);
  border: 1px dashed var(--accent-strong);
  border-radius: var(--radius-sm);
  margin-top: 4px;
}
.new-link-form button {
  margin-top: 4px;
  padding: 6px 14px;
  font-size: 12px;
  font-family: var(--font-display);
  letter-spacing: 0.06em;
  border-radius: var(--radius-sm);
  cursor: pointer;
}
.new-link-form button:disabled {
  opacity: 0.4;
  cursor: not-allowed;
}

/* 链路列表 */
.link-list {
  list-style: none;
  margin: 0;
  padding: 0;
  display: flex;
  flex-direction: column;
  gap: 4px;
}
.link-list li {
  display: flex;
  align-items: flex-start;
  gap: 8px;
  padding: 8px 10px;
  border-radius: var(--radius-sm);
  cursor: pointer;
  border: 1px solid transparent;
  transition: all 0.12s;
}
.link-list li:hover {
  background: var(--bg-primary);
  border-color: var(--border-soft);
}
.link-list li.active {
  background: var(--accent-glow);
  border-color: var(--accent-strong);
}
.link-list .dot {
  width: 8px;
  height: 8px;
  border-radius: 50%;
  margin-top: 4px;
  flex-shrink: 0;
}
.link-info {
  flex: 1;
  min-width: 0;
}
.link-id {
  font-family: var(--font-mono);
  font-size: 13px;
  color: var(--text-main);
  font-weight: 600;
}
.link-meta {
  font-family: var(--font-mono);
  font-size: 10px;
  color: var(--text-faint);
  margin-top: 2px;
}
.link-state {
  font-family: var(--font-mono);
  font-size: 10px;
  margin-top: 2px;
  letter-spacing: 0.05em;
}
.close-btn {
  background: transparent;
  border: none;
  color: var(--text-faint);
  font-size: 16px;
  line-height: 1;
  cursor: pointer;
  padding: 0 4px;
  border-radius: 3px;
}
.close-btn:hover {
  color: var(--error);
  background: rgba(255, 80, 80, 0.08);
}
.empty {
  padding: 12px;
  text-align: center;
  font-size: 11px;
  color: var(--text-faint);
  font-style: italic;
}

/* === 右侧详情 === */
.link-detail {
  flex: 1;
  min-width: 0;
  overflow-y: auto;
  background: var(--bg-elevated);
  border: 1px solid var(--border-soft);
  border-radius: var(--radius-md);
  padding: 16px;
}
.detail-content {
  display: flex;
  flex-direction: column;
  gap: 12px;
}
.detail-header {
  display: flex;
  align-items: center;
  gap: 12px;
  padding-bottom: 12px;
  border-bottom: 1px solid var(--border-soft);
  flex-wrap: wrap;
}
.detail-header h2 {
  font-family: var(--font-mono);
  font-size: 18px;
  color: var(--text-main);
  margin: 0;
  font-weight: 700;
}
.state-tag {
  font-family: var(--font-mono);
  font-size: 11px;
  padding: 2px 8px;
  border: 1px solid;
  border-radius: 3px;
  letter-spacing: 0.05em;
}
.transport-tag {
  font-family: var(--font-mono);
  font-size: 11px;
  padding: 2px 8px;
  background: var(--bg-primary);
  border: 1px solid var(--border);
  border-radius: 3px;
  color: var(--accent-strong);
}
.addr {
  font-family: var(--font-mono);
  font-size: 11px;
  color: var(--text-muted);
  flex: 1;
}
.empty-detail {
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  height: 100%;
  color: var(--text-faint);
}
.empty-icon {
  font-size: 48px;
  color: var(--text-faint);
  margin-bottom: 12px;
  opacity: 0.3;
}
.empty-detail p {
  font-size: 12px;
  margin: 0;
}

/* === 通用表单/按钮（与旧版本对齐）=== */
.form-row {
  display: flex;
  gap: 12px;
  flex-wrap: wrap;
  align-items: flex-end;
  margin-bottom: 8px;
}
.form-row label {
  display: flex;
  flex-direction: column;
  gap: 4px;
  font-size: 11px;
  color: var(--text-muted);
  letter-spacing: 0.04em;
  text-transform: uppercase;
}
.form-row label.grow {
  flex: 1;
  min-width: 200px;
}
.form-row label span {
  font-size: 10px;
}
.form-row input {
  padding: 6px 10px;
  font-size: 13px;
  font-family: var(--font-mono);
  background: var(--bg-primary);
  border: 1px solid var(--border-soft);
  border-radius: var(--radius-sm);
  color: var(--text-main);
  min-width: 80px;
}
.form-row input:focus {
  outline: none;
  border-color: var(--accent-strong);
  box-shadow: 0 0 0 2px var(--accent-glow);
}

.action-row {
  display: flex;
  align-items: center;
  gap: 10px;
  flex-wrap: wrap;
}
.action-row button {
  padding: 6px 14px;
  font-size: 12px;
  font-family: var(--font-display);
  letter-spacing: 0.06em;
  background: var(--bg-primary);
  color: var(--text-main);
  border: 1px solid var(--border-soft);
  border-radius: var(--radius-sm);
  cursor: pointer;
  transition: all 0.12s;
}
.action-row button:hover:not(:disabled) {
  background: var(--accent-glow);
  border-color: var(--accent-strong);
  color: var(--accent-strong);
}
.action-row button:disabled {
  opacity: 0.4;
  cursor: not-allowed;
}
.action-row .msg {
  font-family: var(--font-mono);
  font-size: 11px;
  color: var(--text-muted);
  word-break: break-all;
}

.hint-text {
  font-size: 11px;
  color: var(--text-faint);
  margin: 4px 0 0;
  line-height: 1.5;
}
.hint-text code {
  font-family: var(--font-mono);
  font-size: 10px;
  padding: 1px 4px;
  background: var(--bg-primary);
  border-radius: var(--radius-sm);
  color: var(--accent-strong);
}
</style>
