import {defineStore} from 'pinia'
import {ref, computed} from 'vue'

// ============================================================================
// 主题 store
// ----------------------------------------------------------------------------
// 设计：
//   - 主题 token 在 themes/{elvish,holy}.css，通过 body.theme-xxx 切换
//   - localStorage 持久化用户选择
//   - Google Font 按需加载：启动时加载当前主题字体；切换时加载新主题字体
//   - 组件用 var(--xxx)，不感知主题切换
// ============================================================================

export type ThemeName = 'elvish' | 'holy' | 'dragonborn' | 'emerald' | 'void' | 'abyss'

interface ThemeMeta {
  name: ThemeName
  label: string
  icon: string
  // Google Fonts URL，按需加载（已加载过的 URL 不重复加载）
  fontUrl?: string
  // 字体名（用于 document.fonts.load 等待加载完成）
  fontFamily?: string
}

const THEMES: Record<ThemeName, ThemeMeta> = {
  elvish: {
    name: 'elvish',
    label: '精灵幻境',
    icon: '🌙',
    fontUrl: 'https://fonts.googleapis.com/css2?family=Cinzel+Decorative:wght@400;700&display=swap',
    fontFamily: 'Cinzel Decorative',
  },
  holy: {
    name: 'holy',
    label: '圣白光辉',
    icon: '✦',
    fontUrl: 'https://fonts.googleapis.com/css2?family=Cormorant+Garamond:wght@400;500;600;700&display=swap',
    fontFamily: 'Cormorant Garamond',
  },
  dragonborn: {
    name: 'dragonborn',
    label: '暗夜龙裔',
    icon: '🐉',
    fontUrl: 'https://fonts.googleapis.com/css2?family=Cinzel:wght@400;500;600;700&display=swap',
    fontFamily: 'Cinzel',
  },
  emerald: {
    name: 'emerald',
    label: '翡翠秘境',
    icon: '🌿',
    // Cormorant Garamond 与 holy 共用，已加载过则不重复
    fontUrl: 'https://fonts.googleapis.com/css2?family=Cormorant+Garamond:wght@400;500;600;700&display=swap',
    fontFamily: 'Cormorant Garamond',
  },
  void: {
    name: 'void',
    label: '虚空幽影',
    icon: '🌑',
    fontUrl: 'https://fonts.googleapis.com/css2?family=JetBrains+Mono:wght@400;500;700&display=swap',
    fontFamily: 'JetBrains Mono',
  },
  abyss: {
    name: 'abyss',
    label: '深渊海妖',
    icon: '🌊',
    // Cinzel 与 dragonborn 共用，已加载过则不重复
    fontUrl: 'https://fonts.googleapis.com/css2?family=Cinzel:wght@400;500;600;700&display=swap',
    fontFamily: 'Cinzel',
  },
}

const STORAGE_KEY = 'gw-theme'

// 已加载的字体 URL 集合，避免重复加载
const loadedFontUrls = new Set<string>()

// 当前主题（启动时从 localStorage 读取，默认 elvish）
function readStoredTheme(): ThemeName {
  try {
    const t = localStorage.getItem(STORAGE_KEY) as ThemeName | null
    if (t && THEMES[t]) return t
  } catch { /* localStorage 不可用时 fallback */ }
  return 'elvish'
}

const theme = ref<ThemeName>(readStoredTheme())

// === 字体按需加载 ===
// 动态插入 <link rel="stylesheet"> 加载 Google Font，并用 document.fonts.load
// 等待字体真正可用（避免切换后字体闪烁）。
async function ensureFontLoaded(meta: ThemeMeta): Promise<void> {
  if (!meta.fontUrl || !meta.fontFamily) return
  if (loadedFontUrls.has(meta.fontUrl)) return

  // 插入 <link>
  const link = document.createElement('link')
  link.rel = 'stylesheet'
  link.href = meta.fontUrl
  document.head.appendChild(link)
  loadedFontUrls.add(meta.fontUrl)

  // 等待字体真正可用（最多 5 秒，超时也继续，避免阻塞 UI）
  try {
    await Promise.race([
      (document as any).fonts.load(`16px "${meta.fontFamily}"`),
      new Promise<void>((resolve) => setTimeout(resolve, 5000)),
    ])
  } catch {
    // 字体加载失败不阻塞主题切换
  }
}

// === 应用主题到 body ===
async function applyTheme(name: ThemeName) {
  const meta = THEMES[name]
  // 先加载字体（按需）
  await ensureFontLoaded(meta)
  // 再切换 body class
  document.body.className = `theme-${name}`
}

// === actions ===
async function setTheme(name: ThemeName) {
  if (!THEMES[name]) return
  theme.value = name
  try {
    localStorage.setItem(STORAGE_KEY, name)
  } catch { /* localStorage 不可用时静默 */ }
  await applyTheme(name)
}

// 启动时初始化（在 store 首次实例化时调用）
async function init() {
  await applyTheme(theme.value)
}

export const useThemeStore = defineStore('theme', () => {
  // getters
  const current = computed(() => THEMES[theme.value])
  const all = computed(() => Object.values(THEMES))

  return {
    // state
    theme,
    // getters
    current,
    all,
    // actions
    setTheme,
    init,
  }
})
