import {defineConfig} from 'vite'
import vue from '@vitejs/plugin-vue'

// https://vitejs.dev/config/
export default defineConfig({
  plugins: [vue()],
  server: {
    // 强制监听 IPv4 loopback。
    // wails3 dev 模式下 exe 的 ExternalAssetHandler 用 `dial tcp4 127.0.0.1:9245`
    // 拿前端资源；vite v8 默认 `localhost` 在 Windows 上可能解析成 `::1` (IPv6)，
    // 导致 IPv4 dial 被拒（proxy error: connection actively refused）。
    host: '127.0.0.1',
  },
})
