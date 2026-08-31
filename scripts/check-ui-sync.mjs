#!/usr/bin/env node
// Dual-copy UI kit sync checker (stage 1 of the shared-UI strategy).
// Compares frontends/{admin,user}/src/components/ui byte-for-byte so the two
// copies cannot drift. Exit 0 = in sync, 1 = drift (CI-gateable).
// Stage 2 replaces this script when the kit moves to frontends/packages/ui.

import { readdirSync, readFileSync, existsSync } from 'node:fs'
import { join, dirname } from 'node:path'
import { fileURLToPath } from 'node:url'

const root = join(dirname(fileURLToPath(import.meta.url)), '..')
const dirs = {
  admin: join(root, 'frontends', 'admin', 'src', 'components', 'ui'),
  user: join(root, 'frontends', 'user', 'src', 'components', 'ui'),
}

const files = {
  admin: readdirSync(dirs.admin).filter((f) => f.endsWith('.vue')).sort(),
  user: readdirSync(dirs.user).filter((f) => f.endsWith('.vue')).sort(),
}

const all = [...new Set([...files.admin, ...files.user])].sort()

const report = []
let drift = 0

for (const file of all) {
  const a = join(dirs.admin, file)
  const u = join(dirs.user, file)
  if (!existsSync(a)) {
    report.push(`DRIFT  ${file}  missing in admin`)
    drift++
    continue
  }
  if (!existsSync(u)) {
    report.push(`DRIFT  ${file}  missing in user`)
    drift++
    continue
  }
  const ba = readFileSync(a)
  const bu = readFileSync(u)
  if (ba.equals(bu)) {
    report.push(`OK     ${file}`)
  } else {
    report.push(`DRIFT  ${file}  contents differ`)
    drift++
  }
}

console.log('UI kit sync report (frontends/{admin,user}/src/components/ui)')
console.log(`components checked: ${all.length}`)
console.log('')
for (const line of report) console.log('  ' + line)
console.log('')
if (drift > 0) {
  console.log(`RESULT: ${drift} drifted component(s) — copies must stay byte-identical.`)
  process.exit(1)
} else {
  console.log('RESULT: in sync — all shared UI components are byte-identical.')
}
