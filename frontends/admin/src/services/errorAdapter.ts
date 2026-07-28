/**
 * Frontend_Error_Module — shared, single source of truth for both
 * OAuth2Frontend and OAuth2Admin (Requirement 8.6).
 *
 * SINGLE-SOURCE WIRING (task 10.2 decision)
 * -----------------------------------------
 * The canonical implementation lives in
 *   OAuth2Frontend/src/services/errorAdapter.ts
 *   OAuth2Frontend/src/services/messages/
 * OAuth2Admin and OAuth2Frontend are independent npm packages (no monorepo /
 * workspace) and each is built in an isolated Docker context that copies ONLY
 * its own directory (see OAuth2Admin/Dockerfile `COPY . .`). A cross-app
 * relative import or path alias into OAuth2Frontend would therefore break the
 * containerized build. To keep a single LOGICAL source while remaining
 * build-safe, this file mirrors the canonical source byte-for-byte. The
 * cross-app determinism property test (task 9.7,
 * OAuth2Frontend/src/services/crossAppConsistency.property.test.ts) imports
 * this module and asserts it returns identical messages for every (code,
 * locale). Keep this file and ./messages in lockstep with OAuth2Frontend.
 *
 * `normalizeError` is a pure function that accepts an axios error (or any
 * value) and ALWAYS returns a normalized structure — it NEVER throws
 * (Requirement 8.1). All field access uses optional chaining and type
 * guards so any malformed input safely falls into the generic-unknown or
 * network-fallback branch.
 *
 * Parsing priority (design §9 / Requirements 8.2–8.5):
 *   1. Error Envelope — top-level object whose `error` is an object with a
 *      string `code` → take `error.code` and `error.request_id`.
 *   2. RFC 6749 — top-level `error` is a string → take the top-level `error`.
 *   3. Has a body but matches neither → generic unknown code.
 *   4. No HTTP response (network failure / timeout) → network fallback code,
 *      `httpStatus = 0`.
 *
 * The resolved code is mapped to a localized, user-readable message via the
 * Error_Message_Catalog_FE (`getErrorMessage`). Default locale is `zh-CN`.
 */
import {
  getErrorMessage,
  NETWORK_CODE,
  UNKNOWN_CODE,
  DEFAULT_LOCALE,
} from './messages'

export interface NormalizedError {
  /** Error_Code, OAuth2 protocol code, or a reserved fallback code. */
  code: string
  /** Non-empty localized, user-readable message. */
  message: string
  /** Request_ID when present in the response body, otherwise empty string. */
  request_id: string
  /** HTTP status code, or 0 when there is no HTTP response. */
  httpStatus: number
}

/** Type guard: a non-null plain object (records, arrays excluded conceptually). */
function isObject(value: unknown): value is Record<string, unknown> {
  return typeof value === 'object' && value !== null
}

/** Type guard: a non-empty string. */
function isNonEmptyString(value: unknown): value is string {
  return typeof value === 'string' && value.length > 0
}

/** Brand marker for objects that are already NormalizedError instances. */
const NORMALIZED_BRAND = '__isNormalizedError__'

/** Type guard: an object previously produced by this module (branded). */
function isNormalizedError(value: unknown): value is NormalizedError {
  return isObject(value) && (value as Record<string, unknown>)[NORMALIZED_BRAND] === true
}

/** Attach the non-enumerable brand so `normalizeError` can pass it through. */
function brand(n: NormalizedError): NormalizedError {
  Object.defineProperty(n, NORMALIZED_BRAND, {
    value: true,
    enumerable: false,
    writable: false,
    configurable: false,
  })
  return n
}

/**
 * Normalize any (axios) error into a stable, user-facing structure.
 * Guaranteed to never throw and to always return a non-empty `message`.
 */
export function normalizeError(err: unknown, locale?: string): NormalizedError {
  // Idempotency: a value already normalized by this module (e.g. the
  // session-expired error rejected by the axios interceptor) is returned
  // as-is so views can call `normalizeError(e)` uniformly.
  if (isNormalizedError(err)) {
    return err
  }

  // Resolve locale defensively; an invalid/missing locale falls back to zh-CN.
  const loc = isNonEmptyString(locale) ? locale : DEFAULT_LOCALE

  const errObj = isObject(err) ? err : undefined
  const response = errObj?.['response']
  const responseObj = isObject(response) ? response : undefined

  // Case 4: no HTTP response at all (network failure / timeout). axios leaves
  // `error.response` undefined in this case. (Requirement 8.5)
  if (responseObj === undefined) {
    return {
      code: NETWORK_CODE,
      message: getErrorMessage(NETWORK_CODE, loc),
      request_id: '',
      httpStatus: 0,
    }
  }

  const status = responseObj['status']
  const httpStatus = typeof status === 'number' ? status : 0
  const data = responseObj['data']

  if (isObject(data)) {
    const envelopeError = data['error']

    // Case 1: Error Envelope — `error` is an object with a string `code`.
    // (Requirement 8.2)
    if (isObject(envelopeError) && isNonEmptyString(envelopeError['code'])) {
      const code = envelopeError['code']
      const requestId = isNonEmptyString(envelopeError['request_id'])
        ? envelopeError['request_id']
        : isNonEmptyString(data['request_id'])
          ? data['request_id']
          : ''
      return {
        code,
        message: getErrorMessage(code, loc),
        request_id: requestId,
        httpStatus,
      }
    }

    // Case 2: RFC 6749 protocol error — top-level `error` is a string.
    // (Requirement 8.3)
    if (isNonEmptyString(envelopeError)) {
      const requestId = isNonEmptyString(data['request_id'])
        ? data['request_id']
        : ''
      return {
        code: envelopeError,
        message: getErrorMessage(envelopeError, loc),
        request_id: requestId,
        httpStatus,
      }
    }

    // Case 3: has a body object but matches neither shape → generic unknown.
    // (Requirement 8.4)
    const requestId = isNonEmptyString(data['request_id'])
      ? data['request_id']
      : ''
    return {
      code: UNKNOWN_CODE,
      message: getErrorMessage(UNKNOWN_CODE, loc),
      request_id: requestId,
      httpStatus,
    }
  }

  // There is an HTTP response, but the body is not a parseable object
  // (string, number, null, empty, ...) → generic unknown. (Requirement 8.4)
  return {
    code: UNKNOWN_CODE,
    message: getErrorMessage(UNKNOWN_CODE, loc),
    request_id: '',
    httpStatus,
  }
}

/**
 * Reserved Error_Code used to convey an expired/invalid session
 * (401 with a failed token refresh). Maps to the localized "登录已过期"
 * message in the Error_Message_Catalog_FE.
 */
export const SESSION_EXPIRED_CODE = 'AUTH_TOKEN_EXPIRED'

/**
 * Build a NormalizedError describing an expired session. Shared by both
 * OAuth2Frontend and OAuth2Admin so the 401-refresh-failure path surfaces a
 * single, consistent localized message via the Frontend_Error_Module
 * (Requirement 10.4). Never throws.
 */
export function sessionExpiredError(locale?: string): NormalizedError {
  const loc = isNonEmptyString(locale) ? locale : DEFAULT_LOCALE
  return brand({
    code: SESSION_EXPIRED_CODE,
    message: getErrorMessage(SESSION_EXPIRED_CODE, loc),
    request_id: '',
    httpStatus: 401,
  })
}

export default normalizeError
