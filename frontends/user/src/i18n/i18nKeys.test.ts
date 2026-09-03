// i18n catalog integrity (ADR-0013):
//   1. Key parity — `en` and `zh-CN` UI catalogs expose identical key paths.
//   2. Key existence — every literal `$t('…')` / `t('…')` / `i18n.global.t('…')`
//      call site in src/ resolves against the `en` catalog (CI's tsc gate
//      cannot type-check templates; this is the mechanical net for typos).
//   3. Cross-app `ui.*` parity — byte-synced components/ui consumers must
//      resolve the same `ui.*` subtree in both apps (checked user→admin).
//
// Sources are enumerated via import.meta.glob('?raw') so the test stays
// tsc-clean without @types/node and works identically under vitest.
import { describe, expect, it } from 'vitest'
import { en } from './en'
import { zhCN } from './zh-CN'

type Tree = Record<string, unknown>

function flatten(tree: Tree, prefix = ''): string[] {
  const keys: string[] = []
  for (const [k, v] of Object.entries(tree)) {
    const path = prefix ? `${prefix}.${k}` : k
    if (v && typeof v === 'object' && !Array.isArray(v)) {
      keys.push(...flatten(v as Tree, path))
    } else {
      keys.push(path)
    }
  }
  return keys
}

const enKeys = new Set(flatten(en))
const zhKeys = new Set(flatten(zhCN))

const rawSources = import.meta.glob('../**/*.{vue,ts}', {
  eager: true,
  query: '?raw',
  import: 'default',
}) as Record<string, string>

// Catalog files themselves and test files are out of scope.
const sources = Object.entries(rawSources).filter(
  ([path]) => !path.includes('/i18n/') && !/\.test\.ts$/.test(path) && !/\.spec\.ts$/.test(path),
)

describe('i18n catalog integrity', () => {
  it('en and zh-CN expose identical key paths', () => {
    const onlyEn = [...enKeys].filter((k) => !zhKeys.has(k))
    const onlyZh = [...zhKeys].filter((k) => !enKeys.has(k))
    expect(
      { onlyEn, onlyZh },
      'catalog key drift between en and zh-CN',
    ).toEqual({ onlyEn: [], onlyZh: [] })
  })

  it('every literal t() call site resolves in the en catalog', () => {
    expect(sources.length).toBeGreaterThan(20)

    const unresolved: string[] = []
    // Literal calls: $t('k'), t('k'), i18n.global.t('k') — single/double quoted.
    const literalRe = /(?:\$t|(?<![\w.$])t|i18n\.global\.t)\(\s*['"]([^'"]+)['"]/g
    // Dynamic calls with a static prefix: t(`prefix${…}`)
    const templateRe = /(?:\$t|(?<![\w.$])t|i18n\.global\.t)\(\s*`([^`${}]+)\$\{/g

    for (const [path, text] of sources) {
      for (const m of text.matchAll(literalRe)) {
        if (!enKeys.has(m[1])) unresolved.push(`${path} → ${m[1]}`)
      }
      for (const m of text.matchAll(templateRe)) {
        const prefix = m[1]
        const hasFamily = [...enKeys].some((k) => k.startsWith(prefix))
        if (!hasFamily) unresolved.push(`${path} → dynamic family '${prefix}*'`)
      }
    }
    expect(unresolved, 'unresolved i18n keys at call sites').toEqual([])
  })

  it('ui.* subtree matches the admin app (byte-synced components)', async () => {
    // Same dynamic-import pattern as crossAppConsistency.property.test.ts;
    // the catalog module is dependency-free so node-env vitest resolves it.
    const adminEn = (await import('../../../admin/src/i18n/en')).en as Tree
    expect(adminEn.ui).toEqual(en.ui)
  })
})
