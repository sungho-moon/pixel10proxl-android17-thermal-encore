<template>
  <div class="page flex flex-col bg-background">
    <header class="max-w-3xl w-full mx-auto p-5"><h1 class="text-xl font-semibold">{{ $t('settings_page.title') }}</h1></header>
    <div class="flex-1 min-h-0 overflow-y-auto pb-safe-nav">
      <div class="max-w-3xl mx-auto px-5 space-y-2">
        <div class="md3-list" v-for="entry in entries" :key="entry.path">
          <Ripple @click="$router.push(entry.path)" tabindex="0" class="md3-list-item">
            <div class="flex items-center gap-4 px-5 py-5">
              <component :is="entry.icon" class="text-primary shrink-0" />
              <div class="flex-1"><h2 class="text-sm font-medium">{{ $t(entry.title) }}</h2><p class="text-xs text-on-surface-variant mt-1">{{ $t(entry.description) }}</p></div>
              <Chevron class="text-on-surface-variant shrink-0" />
            </div>
          </Ripple>
        </div>
        <p class="text-xs text-on-surface-variant px-4 py-4">{{ $t('pixel.supported') }}</p>
      </div>
    </div>
  </div>
</template>
<script setup>
import Ripple from '@/components/ui/Ripple.vue'
import Chevron from '@/components/icons/ChevronRight.vue'
import Feather from '@/components/icons/Feather.vue'
import Tune from '@/components/icons/Tune.vue'
import Games from '@/components/icons/Games.vue'
import Language from '@/components/icons/Language.vue'
import Text from '@/components/icons/Text.vue'
const entries=[
  {path:'/settings/high_fps',title:'pixel.high_fps',description:'pixel.high_fps_desc',icon:Games},
  {path:'/settings/lite_mode',title:'pixel.lite',description:'pixel.lite_desc',icon:Feather},
  {path:'/settings/disable_tweaks',title:'pixel.disabled',description:'pixel.disabled_desc',icon:Tune},
  {path:'/settings/fas',title:'pixel.fas',description:'pixel.fas_desc',icon:Tune},
  {path:'/settings/language',title:'settings_page.language.title',description:'settings_page.language.title',icon:Language},
  {path:'/settings/log_level',title:'settings_page.log_level.title',description:'settings_page.log_level.description',icon:Text},
]
</script>
