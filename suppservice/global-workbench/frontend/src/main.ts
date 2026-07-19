import {createApp} from 'vue'
import {createPinia} from 'pinia'
import App from './App.vue'
import router from './router'
import {useThemeStore} from './stores/theme'
import './style.css'

const app = createApp(App)
const pinia = createPinia()
app.use(pinia)
app.use(router)

// 启动时初始化主题（应用 body class + 按需加载字体）
// 不 await，让 UI 先渲染（字体加载是渐进增强）
const themeStore = useThemeStore(pinia)
void themeStore.init()

app.mount('#app')
