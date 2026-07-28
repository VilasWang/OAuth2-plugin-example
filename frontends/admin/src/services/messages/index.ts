/**
 * Error_Message_Catalog_FE — import contract + resolver (mirror of the
 * canonical source in OAuth2Frontend/src/services/messages/index.ts).
 *
 * SINGLE-SOURCE WIRING (task 10.2): OAuth2Admin and OAuth2Frontend are
 * independent npm packages built in isolated Docker contexts, so this catalog
 * is mirrored here to keep a single LOGICAL source while staying build-safe.
 * The cross-app determinism property test (task 9.7) imports this module and
 * asserts it produces identical messages to the Frontend catalog for every
 * (code, locale). Keep this file in lockstep with OAuth2Frontend.
 *
 * Contract (stable):
 *   - `getErrorMessage(code, locale)` → non-empty localized string
 *   - reserved fallback keys `__unknown__` and `__network__`
 *   - `DEFAULT_LOCALE` = `zh-CN`
 *
 * Behavior (Requirements 9.2, 9.3, 9.6):
 *   - Missing locale → fall back to DEFAULT_LOCALE (zh-CN).
 *   - Missing code in the resolved locale → fall back to UNKNOWN_CODE message
 *     and `console.warn` the missing code.
 *   - Always returns a non-empty string.
 */
import { zhCN } from './zh-CN'

/** Reserved key for the generic unknown-error fallback. */
export const UNKNOWN_CODE = '__unknown__'
/** Reserved key for the network/timeout fallback. */
export const NETWORK_CODE = '__network__'
/** Default UI language. */
export const DEFAULT_LOCALE = 'zh-CN'

/** locale → (Error_Code → localized message). Default language is zh-CN. */
export const messages: Record<string, Record<string, string>> = {
  'zh-CN': zhCN,
}

/**
 * Resolve a localized, user-readable message for an Error_Code.
 * Never returns an empty string.
 */
export function getErrorMessage(code: string, locale: string = DEFAULT_LOCALE): string {
  // Resolve the locale table, falling back to the default language (zh-CN).
  const table = messages[locale] ?? messages[DEFAULT_LOCALE]

  const direct = table?.[code]
  if (typeof direct === 'string' && direct.length > 0) {
    return direct
  }

  // Missing key: log the missing code and fall back to the generic unknown.
  // eslint-disable-next-line no-console
  console.warn(`[errorAdapter] missing message for code: ${code} (locale: ${locale})`)

  const fallback = table?.[UNKNOWN_CODE] ?? messages[DEFAULT_LOCALE]?.[UNKNOWN_CODE]
  if (typeof fallback === 'string' && fallback.length > 0) {
    return fallback
  }

  // Last-resort constant guarantees a non-empty string even if the catalog
  // is somehow incomplete.
  return '发生未知错误，请稍后重试'
}
