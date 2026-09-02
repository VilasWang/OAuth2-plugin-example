/**
 * Error_Message_Catalog_FE — per-locale registry + lookup.
 *
 * Contract (stable, consumed by `errorAdapter.ts`):
 *
 *   - `getErrorMessage(code, locale?)` → non-empty localized string.
 *     When `locale` is omitted the **active UI locale** is used
 *     (`services/locale.ts`, pushed by `src/i18n/index.ts`) so one language
 *     switch drives both page chrome and error messages (ADR-0013).
 *   - reserved fallback keys `__unknown__` and `__network__`.
 *   - `DEFAULT_LOCALE` = fallback table used when the requested locale is
 *     not registered (English, since the frontend i18n adoption; zh-CN
 *     remains a fully registered locale).
 *
 * Behavior (Requirements 9.2, 9.3, 9.6):
 *   - Missing locale → fall back to DEFAULT_LOCALE (en).
 *   - Missing code in the resolved locale → fall back to UNKNOWN_CODE message
 *     and `console.warn` the missing code.
 *   - Always returns a non-empty string.
 */
import { en } from './en'
import { zhCN } from './zh-CN'
import { FALLBACK_LOCALE, getCurrentLocale } from '../locale'

/** Reserved key for the generic unknown-error fallback. */
export const UNKNOWN_CODE = '__unknown__'
/** Reserved key for the network/timeout fallback. */
export const NETWORK_CODE = '__network__'
/** Fallback-table locale — NOT the active-locale default (see above). */
export const DEFAULT_LOCALE = FALLBACK_LOCALE

/** locale → (Error_Code → localized message). */
export const messages: Record<string, Record<string, string>> = {
  en,
  'zh-CN': zhCN,
}

/**
 * Resolve a localized, user-readable message for an Error_Code.
 * Never returns an empty string.
 */
export function getErrorMessage(code: string, locale: string = getCurrentLocale()): string {
  // Resolve the locale table, falling back to the default language (en).
  const table = messages[locale] ?? messages[DEFAULT_LOCALE]

  const direct = table?.[code]
  if (typeof direct === 'string' && direct.length > 0) {
    return direct
  }

  // Missing key: log the missing code and fall back to the generic unknown.

  console.warn(`[errorAdapter] missing message for code: ${code} (locale: ${locale})`)

  const fallback = table?.[UNKNOWN_CODE] ?? messages[DEFAULT_LOCALE]?.[UNKNOWN_CODE]
  if (typeof fallback === 'string' && fallback.length > 0) {
    return fallback
  }

  // Last-resort constant guarantees a non-empty string even if the catalog
  // is somehow incomplete (defensive — Property 13 ensures the keys exist).
  return 'An unexpected error occurred. Please try again later.'
}
