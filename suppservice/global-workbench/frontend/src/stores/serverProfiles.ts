import {defineStore} from 'pinia'
import {ref, computed} from 'vue'

// ============================================================================
// 服务器 Profile store
// ----------------------------------------------------------------------------
// 管理多个 SSH 凭据组合，明文存 localStorage（开发机本地工具，可接受）。
// 设计：
//   - profiles 是数组，currentId 是当前选中的 profile id
//   - 切换 profile 时，UI 自动填入对应凭据
//   - 改 input 后由组件调 commitCurrent() 把当前值持久化
//   - 启动时若 localStorage 无数据，自动创建一个默认 profile
// ============================================================================

export interface ServerProfile {
  id: string
  name: string
  host: string
  port: number          // SSH 端口（默认 22）
  user: string
  password: string
  grpcPort: number      // beastserver gRPC 端口（默认 9010），用于 RoomService.CreateRoom
  createdAt: number
}

const STORAGE_KEY = 'gw-server-profiles'
const CURRENT_KEY = 'gw-server-current'

// 生成简单 uuid（不严格，但够用）
function uuid(): string {
  return Date.now().toString(36) + Math.random().toString(36).slice(2, 8)
}

function defaultProfile(): ServerProfile {
  return {
    id: uuid(),
    name: '默认服务器',
    host: '',
    port: 22,
    user: '',
    password: '',
    grpcPort: 9010,
    createdAt: Date.now(),
  }
}

// 兼容旧版本 localStorage 数据：如果 profile 没有 grpcPort 字段，回填 9010。
// 避免老用户升级后 grpcPort=undefined 导致 UI 显示空。
function migrateProfile(p: Partial<ServerProfile>): ServerProfile {
  return {
    ...defaultProfile(),
    ...p,
    grpcPort: p.grpcPort ?? 9010,
  } as ServerProfile
}

function loadProfiles(): ServerProfile[] {
  try {
    const raw = localStorage.getItem(STORAGE_KEY)
    if (!raw) {
      // 首次启动：创建一个默认 profile
      const p = defaultProfile()
      localStorage.setItem(STORAGE_KEY, JSON.stringify([p]))
      localStorage.setItem(CURRENT_KEY, p.id)
      return [p]
    }
    const arr = JSON.parse(raw) as ServerProfile[]
    if (!Array.isArray(arr) || arr.length === 0) {
      const p = defaultProfile()
      localStorage.setItem(STORAGE_KEY, JSON.stringify([p]))
      localStorage.setItem(CURRENT_KEY, p.id)
      return [p]
    }
    // 迁移旧 profile（缺 grpcPort 字段时补默认值）
    return arr.map(migrateProfile)
  } catch {
    const p = defaultProfile()
    return [p]
  }
}

function loadCurrentId(profiles: ServerProfile[]): string | null {
  try {
    const id = localStorage.getItem(CURRENT_KEY)
    if (id && profiles.find(p => p.id === id)) return id
  } catch { /* ignore */ }
  return profiles[0]?.id ?? null
}

const profiles = ref<ServerProfile[]>(loadProfiles())
const currentId = ref<string | null>(loadCurrentId(profiles.value))

const current = computed<ServerProfile | null>(() =>
  profiles.value.find(p => p.id === currentId.value) ?? null
)

function persist() {
  try {
    localStorage.setItem(STORAGE_KEY, JSON.stringify(profiles.value))
    if (currentId.value) {
      localStorage.setItem(CURRENT_KEY, currentId.value)
    }
  } catch { /* localStorage 不可用时静默 */ }
}

// 切换当前 profile
function setCurrent(id: string) {
  if (profiles.value.find(p => p.id === id)) {
    currentId.value = id
    persist()
  }
}

// 把字段更新到当前 profile（用于"保存"按钮）。
// name 不在更新字段里：重命名走专门的 renameCurrent。
function commitCurrent(fields: Pick<ServerProfile, 'host' | 'port' | 'user' | 'password' | 'grpcPort'>) {
  const p = current.value
  if (!p) return
  Object.assign(p, fields)
  persist()
}

// 新建空 profile 并切换为当前
function create(): string {
  const p = defaultProfile()
  p.name = `服务器 ${profiles.value.length + 1}`
  profiles.value.push(p)
  currentId.value = p.id
  persist()
  return p.id
}

// 删除 profile（若删的是当前，自动切到第一个）
function remove(id: string) {
  const idx = profiles.value.findIndex(p => p.id === id)
  if (idx < 0) return
  profiles.value.splice(idx, 1)
  // 如果删完后空了，自动创建一个默认
  if (profiles.value.length === 0) {
    const p = defaultProfile()
    profiles.value.push(p)
    currentId.value = p.id
  } else if (currentId.value === id) {
    currentId.value = profiles.value[0].id
  }
  persist()
}

// 重命名当前 profile
function renameCurrent(name: string) {
  const p = current.value
  if (!p) return
  p.name = name
  persist()
}

export const useServerProfilesStore = defineStore('serverProfiles', () => {
  return {
    // state
    profiles,
    currentId,
    // getters
    current,
    // actions
    setCurrent,
    commitCurrent,
    create,
    remove,
    renameCurrent,
  }
})
