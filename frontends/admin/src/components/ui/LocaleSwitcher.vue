<script setup lang="ts">
import { computed, onBeforeUnmount, onMounted, ref } from 'vue'
import { useI18n } from 'vue-i18n'
import { setLocale, SUPPORTED_LOCALES, type AppLocale } from '../../i18n'

const { locale, t } = useI18n()
const open = ref(false)
const root = ref<HTMLElement | null>(null)
const trigger = ref<HTMLButtonElement | null>(null)

// Native endonyms — shown identically in every locale (i18n convention).
// computed so the labels re-resolve if a locale's table ever changes the
// other locale's endonym spelling (t() at setup top-level would freeze).
const NATIVE_LABELS = computed<Record<AppLocale, string>>(() => ({
  en: t('ui.locale.en'),
  'zh-CN': t('ui.locale.zhCN'),
}))

const SHORT_LABELS: Record<AppLocale, string> = {
  en: 'EN',
  'zh-CN': '中文',
}

function pick(next: AppLocale): void {
  setLocale(next)
  open.value = false
  trigger.value?.focus()
}

function onClickOutside(event: MouseEvent): void {
  if (root.value && !root.value.contains(event.target as Node)) {
    open.value = false
  }
}

// ARIA menu pattern: Escape closes and restores focus to the trigger;
// ArrowDown/ArrowUp cycle focus through the menu items.
function onMenuKeydown(event: KeyboardEvent): void {
  if (event.key === 'Escape') {
    event.preventDefault()
    open.value = false
    trigger.value?.focus()
    return
  }
  if (event.key !== 'ArrowDown' && event.key !== 'ArrowUp') return
  if (!root.value) return
  event.preventDefault()
  const items = Array.from(
    root.value.querySelectorAll<HTMLButtonElement>('button[role="menuitemradio"]'),
  )
  if (items.length === 0) return
  const idx = items.indexOf(document.activeElement as HTMLButtonElement)
  const delta = event.key === 'ArrowDown' ? 1 : -1
  const next = idx === -1 ? 0 : (idx + delta + items.length) % items.length
  items[next].focus()
}

onMounted(() => document.addEventListener('click', onClickOutside))
onBeforeUnmount(() => document.removeEventListener('click', onClickOutside))
</script>

<template>
  <div
    ref="root"
    class="relative"
  >
    <button
      ref="trigger"
      type="button"
      class="flex items-center gap-1.5 px-2 py-1.5 rounded-ctl text-sm text-neutral-600
             hover:bg-neutral-100 transition-colors
             focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-ring"
      :aria-label="t('ui.locale.label')"
      :title="t('ui.locale.label')"
      :aria-expanded="open"
      @click="open = !open"
    >
      <svg
        class="w-4 h-4 text-neutral-400"
        viewBox="0 0 16 16"
        fill="currentColor"
        aria-hidden="true"
      >
        <path
          fill-rule="evenodd"
          d="M8 1a7 7 0 100 14A7 7 0 008 1zm5.9 6.5h-2.42a12.3 12.3 0 00-.9-4.06A5.53 5.53 0 0113.9 7.5zM8 2.55c.75.86 1.38 2.55 1.47 4.95h-2.94C6.62 5.1 7.25 3.41 8 2.55zM4.42 3.44A12.3 12.3 0 003.52 7.5H1.1a5.53 5.53 0 013.32-4.06zM1.1 8.5h2.42c.13 1.47.44 2.85.9 4.06A5.53 5.53 0 011.1 8.5zM8 13.45c-.75-.86-1.38-2.55-1.47-4.95h2.94c-.09 2.4-.72 4.09-1.47 4.95zm3.58-1.89c.46-1.21.77-2.59.9-4.06h2.42a5.53 5.53 0 01-3.32 4.06z"
        />
      </svg>
      <span>{{ SHORT_LABELS[locale as AppLocale] }}</span>
    </button>

    <Transition name="locale-dropdown">
      <div
        v-if="open"
        class="absolute right-0 mt-2 w-40 bg-surface rounded-card shadow-lg border border-neutral-200 py-1 z-50"
        role="menu"
        :aria-label="t('ui.locale.label')"
        @keydown="onMenuKeydown"
      >
        <button
          v-for="loc in SUPPORTED_LOCALES"
          :key="loc"
          type="button"
          role="menuitemradio"
          :aria-checked="locale === loc"
          class="flex items-center justify-between w-full px-4 py-2 text-sm text-neutral-700
                 hover:bg-neutral-50 transition-colors
                 focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-ring rounded-ctl"
          @click="pick(loc)"
        >
          {{ NATIVE_LABELS[loc] }}
          <svg
            v-if="locale === loc"
            class="w-4 h-4 text-brand-600"
            viewBox="0 0 16 16"
            fill="currentColor"
            aria-hidden="true"
          >
            <path
              fill-rule="evenodd"
              d="M12.78 4.22a.75.75 0 010 1.06l-6.5 6.5a.75.75 0 01-1.06 0L2.72 9.28a.75.75 0 111.06-1.06L5.75 10.19l5.97-5.97a.75.75 0 011.06 0z"
            />
          </svg>
        </button>
      </div>
    </Transition>
  </div>
</template>

<style scoped>
.locale-dropdown-enter-active { transition: all 150ms ease-out; }
.locale-dropdown-leave-active { transition: all 100ms ease-in; }
.locale-dropdown-enter-from { opacity: 0; transform: translateY(-8px) scale(0.96); }
.locale-dropdown-leave-to { opacity: 0; transform: translateY(-4px) scale(0.98); }
</style>
