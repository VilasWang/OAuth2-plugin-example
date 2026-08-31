import { ref, computed } from 'vue'
import { defineStore } from 'pinia'

// Theme wiring (P4): persisted choice in localStorage, applied as
// html[data-theme]. 'system' follows prefers-color-scheme and hot-switches
// when the OS preference changes.
export type ThemePreference = 'light' | 'dark' | 'system'

const STORAGE_KEY = 'fulla-theme'

function readStored(): ThemePreference {
  try {
    const v = localStorage.getItem(STORAGE_KEY)
    if (v === 'light' || v === 'dark' || v === 'system') return v
  } catch { /* storage unavailable (private mode) — follow system */ }
  return 'system'
}

function systemPrefersDark(): boolean {
  return window.matchMedia('(prefers-color-scheme: dark)').matches
}

function applyTheme(pref: ThemePreference) {
  const dark = pref === 'dark' || (pref === 'system' && systemPrefersDark())
  document.documentElement.setAttribute('data-theme', dark ? 'dark' : 'light')
}

export const useThemeStore = defineStore('theme', () => {
  const preference = ref<ThemePreference>(readStored())

  // Effective mode ('light' | 'dark') — what the DOM currently shows.
  const mode = computed<'light' | 'dark'>(() =>
    preference.value === 'dark'
      || (preference.value === 'system' && systemPrefersDark())
      ? 'dark'
      : 'light',
  )

  function set(next: ThemePreference) {
    preference.value = next
    try {
      localStorage.setItem(STORAGE_KEY, next)
    } catch { /* non-fatal */ }
    applyTheme(next)
  }

  function toggle() {
    set(mode.value === 'dark' ? 'light' : 'dark')
  }

  function init() {
    applyTheme(preference.value)
    // Hot-switch on OS preference changes while in 'system' mode.
    window.matchMedia('(prefers-color-scheme: dark)').addEventListener('change', () => {
      if (preference.value === 'system') applyTheme('system')
    })
  }

  return { preference, mode, set, toggle, init }
})
