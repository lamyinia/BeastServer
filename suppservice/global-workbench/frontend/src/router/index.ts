import {createRouter, createWebHashHistory, type RouteRecordRaw} from 'vue-router'

// 路由表：与 Sidebar 树形目录一一对应。
// 命名约定：name 唯一、path 短、component 懒加载（vite 自动分包）。
const routes: RouteRecordRaw[] = [
  {
    path: '/',
    redirect: '/dashboard',
  },
  {
    path: '/dashboard',
    name: 'dashboard',
    component: () => import('../pages/Dashboard.vue'),
    meta: {title: '工作台首页', icon: 'home'},
  },
  {
    path: '/server',
    name: 'server-root',
    redirect: '/server/control',
    meta: {title: 'BeastServer', icon: 'server'},
    children: [
      {
        path: 'control',
        name: 'server-control',
        component: () => import('../pages/server/ServerControl.vue'),
        meta: {title: '进程控制', icon: 'power'},
      },
    ],
  },
  {
    path: '/sdk_event',
    name: 'sdk-event',
    component: () => import('../pages/sdk_event/SdkEvent.vue'),
    meta: {title: 'go-sdk 自检', icon: '◇'},
  },
  {
    path: '/:pathMatch(.*)*',
    name: 'not-found',
    component: () => import('../pages/Dashboard.vue'),
    meta: {title: '404'},
  },
]

const router = createRouter({
  history: createWebHashHistory(),
  routes,
})

export default router
