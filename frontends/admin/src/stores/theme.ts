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

function applyTheme(pref: ThemePreference, osDark: boolean) {
  const dark = pref === 'dark' || (pref === 'system' && osDark)
  document.documentElement.setAttribute('data-theme', dark ? 'dark' : 'light')
}

export const useThemeStore = defineStore('theme', () => {
  const preference = ref<ThemePreference>(readStored())
  // Reactive mirror of the OS preference. matchMedia().matches itself is
  // NOT reactive — without this ref, a system-mode OS switch updates the
  // DOM but `mode` (and every UI binding on it: toggle icon, label,
  // aria-label) keeps the stale value, and toggle() writes back what is
  // already displayed.
  const osDark = ref(systemPrefersDark())

  // Effective mode ('light' | 'dark') — what the DOM currently shows.
  const mode = computed<'light' | 'dark'>(() =>
    preference.value === 'dark'
      || (preference.value === 'system' && osDark.value)
      ? 'dark'
      : 'light',
  )

  function set(next: ThemePreference) {
    preference.value = next
    try {
      localStorage.setItem(STORAGE_KEY, next)
    } catch { /* non-fatal */ }
    applyTheme(next, osDark.value)
  }

  function toggle() {
    set(mode.value === 'dark' ? 'light' : 'dark')
  }

  function init() {
    // Themed must never block app mount — a throw here (exotic embedded
    // webviews, stubbed matchMedia) would leave a white screen.
    try {
      applyTheme(preference.value, osDark.value)
      window.matchMedia('(prefers-color-scheme: dark)').addEventListener('change', (e) => {
        osDark.value = e.matches
        if (preference.value === 'system') applyTheme('system', osDark.value)
      })
    } catch { /* keep default light presentation */ }
  }

  return { preference, mode, set, toggle, init }
})
