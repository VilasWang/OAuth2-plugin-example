import { createI18n } from 'vue-i18n'
import { en } from './en'
import { zhCN } from './zh-CN'
import {
  FALLBACK_LOCALE,
  setCurrentLocale,
  SUPPORTED_LOCALES,
  type AppLocale,
} from '../services/locale'

export const LOCALE_STORAGE_KEY = 'fulla-locale'

function readStoredLocale(): AppLocale | null {
  try {
    const v = window.localStorage.getItem(LOCALE_STORAGE_KEY)
    return SUPPORTED_LOCALES.includes(v as AppLocale) ? (v as AppLocale) : null
  } catch {
    return null // storage unavailable (private mode) — follow the browser
  }
}

/** Best-match the browser languages: any zh region → zh-CN, en* → en. */
function localeFromNavigator(): AppLocale {
  const langs = typeof navigator !== 'undefined'
    ? (navigator.languages ?? [navigator.language])
    : []
  for (const lang of langs) {
    if (typeof lang !== 'string') continue
    const lower = lang.toLowerCase()
    if (lower.startsWith('zh')) return 'zh-CN'
    if (lower.startsWith('en')) return 'en'
  }
  return FALLBACK_LOCALE
}

function detectInitialLocale(): AppLocale {
  if (typeof window === 'undefined') return FALLBACK_LOCALE
  return readStoredLocale() ?? localeFromNavigator()
}

export const i18n = createI18n({
  legacy: false,
  globalInjection: true,
  locale: detectInitialLocale(),
  fallbackLocale: FALLBACK_LOCALE,
  messages: {
    en,
    'zh-CN': zhCN,
  },
})

function applyLocale(locale: AppLocale, persist: boolean): void {
  i18n.global.locale.value = locale
  setCurrentLocale(locale)
  document.documentElement.lang = locale
  if (persist) {
    try {
      window.localStorage.setItem(LOCALE_STORAGE_KEY, locale)
    } catch {
      /* storage unavailable — keep the in-memory switch */
    }
  }
}

/** User-facing switch: applies, persists, and syncs <html lang>. */
export function setLocale(locale: AppLocale): void {
  applyLocale(locale, true)
}

/**
 * Resolve the initial locale before app.mount: pushes the detected locale to
 * the service layer and syncs <html lang> so there is no language flash.
 * Mirrors the theme store's init() pattern (called from main.ts).
 */
export function initI18n(): void {
  applyLocale(i18n.global.locale.value as AppLocale, false)
}

export { FALLBACK_LOCALE, SUPPORTED_LOCALES }
export type { AppLocale }
