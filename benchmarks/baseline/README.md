# Performance baseline archive

This directory holds the **pinned reference group** for performance A/B
comparisons — a frozen copy of one dated result group from
`benchmarks/results/`, so that comparisons stay anchored even as new result
groups land in `results/` (where `gen-comparison.py` silently switches to the
newest `(date, sha)` group).

## Current baseline: `20260819-97c9254`

| | |
|---|---|
| **Group** | `20260819-97c9254-*` (date 2026-08-19, git sha `97c9254`, branch `feat/competitor-benchmark`) |
| **Scenarios** | S1 / S2 / S3 / S5 / S6 — **5 scenarios × 8 levels** (c2 c4 c8 c16 c32 c64 c128 — see filenames) |
| **S4 NOT included** | The 97c9254 session did not re-run S4. The only S4 group is `20260812-1702246` (6 levels, no c128) — use it for cross-week directional reference only, never for same-day A/B. |
| **Measured params** | Per level: **5 s warmup + 10 s measured** (`WARMUP_S=5 DURATION_S=10`, the session protocol of `run-fulla-session.sh`) — **not** the `run-scenario.sh` defaults of 10 s + 30 s. Future A/B runs must reuse the measured params (same windows) to stay comparable. |
| **Stack config** | bench overlay at that sha: cache enabled (client TTL 300 s / token 60 s), PG pool 64 / Redis pool 64, `auto_batch=true`, PG17 + instance tuning, V025 audit partitioning, V026 redundant-index cleanup applied. |
| **Provenance** | Full re-run after the split-session merge (commit `b3b96ca`); replaces `20260818-3c1ced3` as the anchor. |

## Conventions

- A/B verdicts (QPS ≥ +5% with P99 not worse than −5%, or P99 improvement
  ≥ 10%) must compare against a **same-day back-to-back** run of the opposing
  arm — never against this archive across days (cross-day variance on this
  machine is real, e.g. S3/S6 ±8–9%).
- This archive is for **trend anchoring and report tables** (knee points,
  steady capacity, scenario ratios), not for accept/reject decisions.
- When a new group is blessed as the baseline (after a merged optimization
  wave), copy it here, update this README, and drop the old group — exactly
  one baseline group lives here at a time.
