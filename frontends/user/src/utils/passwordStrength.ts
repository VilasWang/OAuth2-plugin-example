/**
 * Password strength score for the register-page meter (mockup 17).
 *
 * Returns 0–4; the meter renders 4 segments, so the scale must be able to
 * reach 4 (length floor + length comfort + 2 char classes + 3 char
 * classes). Segment 1 is the hard floor; the hard floor itself is the
 * backend's auth.min_password_length (RuleSet D4, default 8 across all
 * shipped configs) — the register form's client-side guard and copy
 * ("Minimum 8 characters") track that knob, not this constant.
 */
export function passwordStrength(pw: string): number {
  if (!pw) return 0
  let score = 0
  if (pw.length >= 8) score++
  if (pw.length >= 12) score++
  const classes = [/[a-z]/, /[A-Z]/, /\d/, /[^a-zA-Z0-9]/].filter(r => r.test(pw)).length
  if (classes >= 2) score++
  if (classes >= 3) score++
  return Math.min(score, 4)
}
