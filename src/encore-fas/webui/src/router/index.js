import { createRouter, createWebHashHistory } from 'vue-router'
import { WXEventHandler } from 'webuix'

// Pages
import Home from '@/views/Home.vue'
import Games from '@/views/Games.vue'
import Settings from '@/views/Settings.vue'

import PixelPreference from '@/views/PixelPreference.vue'
import FasPreference from '@/views/FasPreference.vue'
// Setting pages
import GameSettings from '@/views/GameSettings.vue'
import LanguageSelection from '@/views/LanguageSelection.vue'
import LogLevelSelection from '@/views/LogLevelSelection.vue'

window.wx = new WXEventHandler()

const routes = [
  {
    path: '/',
    name: 'Home',
    component: Home,
  },
  {
    path: '/games',
    name: 'Games',
    component: Games,
  },
  {
    path: '/settings',
    name: 'Settings',
    component: Settings,
  },

  // Setting Pages
  {
    path: '/games/:packageName',
    name: 'GameSettings',
    component: GameSettings,
  },
  {
    path: '/settings/lite_mode',
    name: 'LiteMode',
    component: PixelPreference,
    props: { field: 'enforce_lite_mode', label: 'lite' },
  },
  {
    path: '/settings/disable_tweaks',
    name: 'DisableTweaks',
    component: PixelPreference,
    props: { field: 'disable_tweaks', label: 'disabled' },
  },
  {
    path: '/settings/language',
    name: 'LanguageSelection',
    component: LanguageSelection,
  },
  {
    path: '/settings/log_level',
    name: 'LogLevelSelection',
    component: LogLevelSelection,
  },

  {
    path: '/settings/fas',
    name: 'FasPreference',
    component: FasPreference,
    props: { marker: 'disable-fas', label: 'fas' },
  },
  {
    path: '/settings/fas_rs_advisor',
    name: 'FasRsAdvisorPreference',
    component: FasPreference,
    props: { marker: 'disable-fas-rs-advisor', label: 'fas_rs_advisor' },
  },

  { path: '/settings/high_fps', name: 'HighFPS', component: PixelPreference, props: { field: 'high_fps', label: 'high_fps' } },
  // catch-all route
  {
    path: '/:pathMatch(.*)*',
    redirect: '/',
  },
]

const router = createRouter({
  history: createWebHashHistory(),
  routes,
})

router.beforeEach((to, from, next) => {
  next()
})

// Handle back button event (WebUI X API)
wx.on(window, 'back', () => {
  const current = router.currentRoute.value.path || '/'

  if (current === '/') {
    window.ksu?.exit?.()
    return
  }

  // Push route /somepage/detail -> /somepage
  const segments = current.split('/').filter(Boolean)
  const parentPath = segments.length > 1 ? '/' + segments.slice(0, -1).join('/') : '/'
  router.push(parentPath)
})

export default router
