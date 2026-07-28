import { describe, it, expect } from 'vitest'
import fc from 'fast-check'

// Trivial smoke test confirming the vitest + fast-check infrastructure works.
// This will be replaced by the real property-based tests in later tasks.
describe('test infrastructure smoke test', () => {
  it('runs a basic assertion', () => {
    expect(1 + 1).toBe(2)
  })

  it('runs a fast-check property', () => {
    fc.assert(
      fc.property(fc.integer(), fc.integer(), (a, b) => {
        return a + b === b + a
      }),
      { numRuns: 100 },
    )
  })
})
