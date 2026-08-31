import { describe, it, expect } from 'vitest'
import { passwordStrength } from '../../src/utils/passwordStrength'

describe('passwordStrength (register meter scale)', () => {
  it('empty password scores 0 (meter hidden)', () => {
    expect(passwordStrength('')).toBe(0)
  })

  it('below the length floor only class-mixing scores, never length', () => {
    // 7 chars, 4 classes — under the floor of 8, so no length points;
    // mixing alone earns 2 (meter shows warning, not error-only).
    expect(passwordStrength('aB3!aB3')).toBe(2)
    // 7 chars, one class — nothing at all.
    expect(passwordStrength('aaaaaaa')).toBe(0)
  })

  it('floor-length single-class password reaches segment 1', () => {
    expect(passwordStrength('12345678')).toBe(1)
  })

  it('two classes at floor length reaches segment 2', () => {
    expect(passwordStrength('password12')).toBe(2)
  })

  it('three classes at floor length reaches segment 3', () => {
    expect(passwordStrength('Password12')).toBe(3)
  })

  it('length comfort + three classes lights all 4 segments', () => {
    // The 4th segment is reachable — the scale must match the 4-segment UI.
    expect(passwordStrength('Password123!')).toBe(4)
  })

  it('score never exceeds 4', () => {
    expect(passwordStrength('Long!Passphrase#With$Symbols2026')).toBe(4)
  })
})
