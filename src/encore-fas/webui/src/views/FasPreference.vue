<template>
  <div class="page flex flex-col bg-background">
    <header class="max-w-3xl w-full mx-auto flex items-center gap-4 p-5">
      <button @click="$router.back()" aria-label="Back"><ArrowLeft class="w-6 h-6" /></button>
      <h1 class="text-xl font-semibold">{{ $t(`pixel.${label}`) }}</h1>
    </header>
    <div class="max-w-3xl w-full mx-auto p-5 space-y-5">
      <p class="text-on-surface-variant leading-relaxed">{{ $t(`pixel.${label}_desc`) }}</p>
      <div class="bg-surface-container rounded-2xl p-5 flex items-center justify-between gap-4">
        <span>{{ $t(`pixel.${label}`) }}</span>
        <ToggleSwitch :model-value="value" :disabled="!loaded || busy" @update:model-value="save" />
      </div>
      <p v-if="error" role="alert" class="text-red-500 text-sm break-words">{{ error }}</p>
      <p v-else-if="busy || saved" role="status" class="text-sm text-primary">
        {{ $t(busy ? 'pixel.saving' : 'pixel.saved') }}
      </p>
    </div>
  </div>
</template>

<script setup>
import { ref, onActivated } from 'vue'
import { useI18n } from 'vue-i18n'
import { useHomeStore } from '@/stores/Home'
import * as KernelSU from '@/helpers/KernelSU'
import ArrowLeft from '@/components/icons/ArrowLeft.vue'
import ToggleSwitch from '@/components/ui/ToggleSwitch.vue'

const props = defineProps({ marker: String, label: String })
const { t } = useI18n()
const homeStore = useHomeStore()
const value = ref(false)
const loaded = ref(false)
const busy = ref(false)
const saved = ref(false)
const error = ref('')

onActivated(async () => {
  loaded.value = false
  saved.value = false
  error.value = ''
  try {
    value.value = !(await KernelSU.fileExists(
      `/data/adb/.config/encore_pixel_cp41/${props.marker}`,
    ))
    loaded.value = true
  } catch (e) {
    error.value = e.message
  }
})

async function save(next) {
  if (!loaded.value || busy.value) return
  busy.value = true
  saved.value = false
  error.value = ''
  try {
    await KernelSU.writeMarker(props.marker, !next)
    value.value = next
    await homeStore.getServiceState()
    saved.value = true
  } catch (e) {
    error.value = t('pixel.save_error') + ' ' + e.message
  } finally {
    busy.value = false
  }
}
</script>
