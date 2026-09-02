/**
 * Active-UI-locale state for the service layer.
 *
 * Dependency-free on purpose: `errorAdapter.ts` and `services/messages/`
 * are mirrored across frontends/user and frontends/admin and are imported
 * by node-env unit tests (vitest) — they must not touch the DOM or pull in
 * app-only modules (vue-i18n, catalogs). `src/i18n/index.ts` owns
 * detection/persistence and pushes the resolved locale here via
 * `setCurrentLocale()`, so one switch drives both page chrome and error
 * messages (ADR-0013).
 */
export type AppLocale = 'en' | 'zh-CN'

export const SUPPORTED_LOCALES: readonly AppLocale[] = ['en', 'zh-CN']

/** Fallback locale used when a requested locale table is missing. */
export const FALLBACK_LOCALE: AppLocale = 'en'

let current: AppLocale = FALLBACK_LOCALE

export function getCurrentLocale(): AppLocale {
  return current
}

export function setCurrentLocale(locale: AppLocale): void {
  current = locale
}
