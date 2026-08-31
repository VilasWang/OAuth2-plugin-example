#!/usr/bin/env node
// Dual-copy UI kit sync checker (stage 1 of the shared-UI strategy).
// Compares frontends/{admin,user}/src/components/ui recursively so the two
// copies cannot drift. Exit 0 = in sync, 1 = drift (CI-gateable; wired into
// .github/workflows/_frontend.yml). Stage 2 replaces this script when the
// kit moves to frontends/packages/ui.

import { readdirSync, readFileSync, statSync, existsSync } from 'node:fs'
import { join, dirname, relative } from 'node:path'
import { fileURLToPath } from 'node:url'

const root = join(dirname(fileURLToPath(import.meta.url)), '..')
const dirs = {
  admin: join(root, 'frontends', 'admin', 'src', 'components', 'ui'),
  user: join(root, 'frontends', 'user', 'src', 'components', 'ui'),
}

function walkVue(dir, prefix = '') {
  // Recursive: future component subdirectories must not silently escape.
  if (!existsSync(dir)) return null
  const out = []
  for (const entry of readdirSync(dir).sort()) {
    const full = join(dir, entry)
    if (statSync(full).isDirectory()) {
      out.push(...walkVue(full, `${prefix}${entry}/`))
    } else if (entry.endsWith('.vue')) {
      out.push({ rel: `${prefix}${entry}`, full })
    }
  }
  return out
}

const files = {}
for (const [port, dir] of Object.entries(dirs)) {
  const walked = walkVue(dir)
  if (walked === null) {
    console.error(`RESULT: ${port} components/ui directory is missing — cannot verify sync.`)
    process.exit(1)
  }
  if (walked.length === 0) {
    // An empty directory would trivially "match" an empty peer — that is a
    // broken state, not a pass.
    console.error(`RESULT: ${port} components/ui has no .vue files — refusing to report a vacuous pass.`)
    process.exit(1)
  }
  files[port] = walked
}

const byRel = {
  admin: new Map(files.admin.map(f => [f.rel, f])),
  user: new Map(files.user.map(f => [f.rel, f])),
}
const allRels = [...new Set([...byRel.admin.keys(), ...byRel.user.keys()])].sort()

const report = []
let drift = 0

for (const rel of allRels) {
  const a = byRel.admin.get(rel)
  const u = byRel.user.get(rel)
  if (!a) {
    report.push(`DRIFT  ${rel}  missing in admin`)
    drift++
  } else if (!u) {
    report.push(`DRIFT  ${rel}  missing in user`)
    drift++
  } else if (readFileSync(a.full).equals(readFileSync(u.full))) {
    report.push(`OK     ${rel}`)
  } else {
    report.push(`DRIFT  ${rel}  contents differ`)
    drift++
  }
}

console.log(`UI kit sync report (${relative(root, dirs.admin)} vs ${relative(root, dirs.user)})`)
console.log(`components checked: ${allRels.length}`)
console.log('')
for (const line of report) console.log('  ' + line)
console.log('')
if (drift > 0) {
  console.log(`RESULT: ${drift} drifted component(s) — copies must stay byte-identical.`)
  process.exit(1)
} else {
  console.log('RESULT: in sync — all shared UI components are byte-identical.')
}
