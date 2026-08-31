#!/usr/bin/env node
/**
 * color-codemod.mjs — "Machined Trust" P0 palette migration.
 *
 * Rewrites bare Tailwind palette classes in frontends/{admin,user}/src
 * to the @theme token namespace (see design-tokens.css):
 *
 *   gray|zinc|slate|stone-N -> neutral-N          (1:1)
 *   indigo-N                -> brand-N            (1:1)
 *   blue-N                  -> brand-N            (1:1)
 *   sky-50..500             -> brand-N            (same step)
 *   sky-600..900 (bg/gradient)  -> brand step-100 (600->500, 700->600, ...)
 *   sky-600..900 (text/border/ring) -> brand-700  (deep step keeps contrast)
 *   emerald-N -> success-N, rose|red-N -> error-N, amber-N -> warning-N
 *       (target ramps lack 300/400/800/900 -> clamped to nearest step)
 *
 * bg-white and bg-neutral-50 are REPORTED, not rewritten: they need a
 * human decision between "surface" (bg-surface / bg-page) and literal
 * contexts (text contrast, chart colors).
 *
 * Usage:
 *   node scripts/frontend/color-codemod.mjs --check            # report only
 *   node scripts/frontend/color-codemod.mjs --write            # apply
 *   node scripts/frontend/color-codemod.mjs --write <filter>   # substring
 *                                                                file filter
 */
import { readFileSync, writeFileSync, readdirSync, statSync } from 'node:fs'
import { join, relative, resolve, dirname } from 'node:path'
import { fileURLToPath } from 'node:url'

const root = resolve(dirname(fileURLToPath(import.meta.url)), '..', '..')
const targets = ['frontends/admin/src', 'frontends/user/src']
const mode = process.argv[2]
const filter = process.argv[3]

if (mode !== '--check' && mode !== '--write') {
  console.error('usage: color-codemod.mjs --check|--write [file-filter]')
  process.exit(1)
}

function walk(dir, out = []) {
  for (const entry of readdirSync(dir)) {
    const p = join(dir, entry)
    if (statSync(p).isDirectory()) walk(p, out)
    else if (p.endsWith('.vue')) out.push(p)
  }
  return out
}

const CLAMP = { 300: 200, 400: 500, 800: 700, 900: 700 }
const clamp = (family, step) =>
  ['success', 'error', 'warning'].includes(family) && CLAMP[step]
    ? CLAMP[step]
    : step

const CLASS_RE =
  /((?:[a-z-]+:)*)((?:bg|text|border|ring|outline|from|via|to|divide|accent|caret|fill|stroke)-)(gray|zinc|slate|stone|indigo|sky|blue|emerald|rose|red|amber)-(\d{2,3})\b/g

function mapMatch(modifiers, util, family, stepStr) {
  const step = parseInt(stepStr, 10)
  const ctx = util.replace(/-$/, '')
  let target
  let newStep = step

  if (family === 'gray' || family === 'zinc' || family === 'slate' || family === 'stone') {
    target = 'neutral'
  } else if (family === 'indigo' || family === 'blue') {
    target = 'brand'
  } else if (family === 'sky') {
    target = 'brand'
    if (step >= 600) {
      if (ctx === 'text' || ctx === 'border' || ctx === 'ring' || ctx === 'outline') {
        newStep = 700
      } else {
        newStep = step - 100 // bg / gradient: one step lighter
      }
    }
  } else if (family === 'emerald') {
    target = 'success'
    newStep = clamp(target, step)
  } else if (family === 'rose' || family === 'red') {
    target = 'error'
    newStep = clamp(target, step)
  } else if (family === 'amber') {
    target = 'warning'
    newStep = clamp(target, step)
  }

  if (target === family && newStep === step) return null
  return `${modifiers}${util}${target}-${newStep}`
}

let changedFiles = 0
let replacements = 0
const perFamily = {}
const flagged = []

for (const t of targets) {
  const dir = join(root, t)
  for (const file of walk(dir)) {
    if (filter && !file.includes(filter)) continue
    const rel = relative(root, file)
    const src = readFileSync(file, 'utf8')

    let count = 0
    const out = src.replace(CLASS_RE, (m, mods, util, fam, step) => {
      const mapped = mapMatch(mods, util, fam, step)
      if (!mapped) return m
      count++
      replacements++
      const key = `${fam}->${mapped.replace(mods, '').replace(util, '').split('-')[0]}`
      perFamily[key] = (perFamily[key] || 0) + 1
      return mapped
    })

    // surface-context flags (never auto-rewritten)
    for (const [i, line] of src.split('\n').entries()) {
      if (/\bbg-white\b/.test(line) || /\bbg-neutral-50\b/.test(line)) {
        flagged.push(`${rel}:${i + 1}: ${line.trim().slice(0, 100)}`)
      }
    }

    if (count > 0) {
      changedFiles++
      if (mode === '--write') writeFileSync(file, out, 'utf8')
      console.log(`${mode === '--write' ? 'rewrote' : 'would rewrite'} ${rel} (${count})`)
    }
  }
}

console.log('\n--- summary ---')
console.log(`files: ${changedFiles}, replacements: ${replacements} (${mode})`)
for (const [k, v] of Object.entries(perFamily).sort()) console.log(`  ${k}: ${v}`)
if (flagged.length) {
  console.log(`\n--- bg-white / bg-neutral-50 flagged for human review (${flagged.length}) ---`)
  for (const f of flagged) console.log(`  ${f}`)
}
