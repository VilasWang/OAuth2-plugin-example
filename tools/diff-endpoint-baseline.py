#!/usr/bin/env python3
"""Diff the current HTTP endpoint signature surface against the P0 baseline.

Per requirements R15.9 / Property 2, this tool is the gate that every phase
(starting at P3 acceptance gate) runs to assert HTTP behavior equivalence.
P0 backed it with the static contract layer extracted from
`apps/server/openapi.yaml`. A future P7 backfill will extend this to a
live response capture via `--live <captured-snapshot>`.

Inputs:
  --openapi  <path>    OpenAPI 3.x spec to extract from (default:
                       apps/server/openapi.yaml).
  --baseline <path>    Baseline file produced by parse_endpoints.py
                       (default: tools/refactor-baseline/endpoints/openapi.signature.txt).
  --update-baseline    Overwrite the baseline with the current signature
                       (use sparingly: only at P0 capture or after a
                       reviewer-approved P12 ratification).
  --print              Print the current signature instead of diffing.
  --selftest           Run a small fixture-based self-test.

Exit codes:
  0  current signature == baseline (or --update-baseline succeeded).
  1  diff detected. The diff is printed as additions / removals / changes.
  2  invalid invocation or environment error.

Spec references:
  _Design: Property 2, §12.2, §12.6_
  _Requirements: 15.1, 15.2, 15.4, 15.6, 15.9, 15.11, 15.12_
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path
from typing import Dict, List, Tuple

# Reuse the parse_endpoints extractor so the two stay in lockstep.
HERE = Path(__file__).resolve().parent
BASELINE_DIR = HERE / "refactor-baseline"
sys.path.insert(0, str(BASELINE_DIR))

import parse_endpoints  # type: ignore  # noqa: E402

DEFAULT_OPENAPI = HERE.parent / "apps" / "server" / "openapi.yaml"
DEFAULT_BASELINE = BASELINE_DIR / "endpoints" / "openapi.signature.txt"


# ---------------------------------------------------------------------------
# Diff core
# ---------------------------------------------------------------------------


def _row_key(row: List[str]) -> Tuple[str, str]:
    """Identity key for a baseline record: (METHOD, PATH)."""
    return row[0], row[1]


def _index(rows: List[List[str]]) -> Dict[Tuple[str, str], List[str]]:
    out: Dict[Tuple[str, str], List[str]] = {}
    for row in rows:
        out[_row_key(row)] = row
    return out


def _split_baseline(text: str) -> List[List[str]]:
    return [
        ln.split("\t")
        for ln in text.splitlines()
        if ln and not ln.startswith("#")
    ]


def _diff(baseline: List[List[str]], current: List[List[str]]):
    base_idx = _index(baseline)
    cur_idx = _index(current)
    additions = sorted(set(cur_idx) - set(base_idx))
    removals = sorted(set(base_idx) - set(cur_idx))
    changes: List[Tuple[Tuple[str, str], List[str], List[str]]] = []
    for key in sorted(set(base_idx) & set(cur_idx)):
        if base_idx[key] != cur_idx[key]:
            changes.append((key, base_idx[key], cur_idx[key]))
    return additions, removals, changes, base_idx, cur_idx


def _format_row(row: List[str]) -> str:
    return "\t".join(row)


def _format_field_diff(base: List[str], cur: List[str]) -> str:
    fields = ["METHOD", "PATH", "STATUSES", "CONTENT_TYPES", "TAGS", "SECURITY"]
    diffs: List[str] = []
    for i, name in enumerate(fields):
        b = base[i] if i < len(base) else ""
        c = cur[i] if i < len(cur) else ""
        if b != c:
            diffs.append(f"{name}: {b!r} -> {c!r}")
    return "; ".join(diffs)


def report(
    additions, removals, changes, base_idx, cur_idx, *, out=sys.stdout
) -> int:
    if not additions and not removals and not changes:
        out.write(
            f"[diff-endpoint-baseline] OK: {len(base_idx)} endpoint signatures match.\n"
        )
        return 0
    out.write(
        "[diff-endpoint-baseline] diff detected against P0 baseline:\n"
        f"  additions = {len(additions)}\n"
        f"  removals  = {len(removals)}\n"
        f"  changes   = {len(changes)}\n"
    )
    for key in additions:
        out.write(f"  + {_format_row(cur_idx[key])}\n")
    for key in removals:
        out.write(f"  - {_format_row(base_idx[key])}\n")
    for key, base_row, cur_row in changes:
        out.write(
            f"  ~ {key[0]} {key[1]}\n"
            f"      {_format_field_diff(base_row, cur_row)}\n"
        )
    return 1


# ---------------------------------------------------------------------------
# Self-test
# ---------------------------------------------------------------------------


def _selftest() -> int:
    fixture = BASELINE_DIR / "fixtures" / "openapi-mini.yaml"
    if not fixture.exists():
        print(f"[diff-endpoint-baseline][err] missing fixture: {fixture}", file=sys.stderr)
        return 2

    doc = parse_endpoints._load_yaml(fixture.read_text(encoding="utf-8"))
    rows = parse_endpoints.extract_endpoints(doc)

    # Round-trip: rendering + re-splitting must be lossless.
    rendered = parse_endpoints.render(rows)
    rt = _split_baseline(rendered)
    if [list(r) for r in rt] != [list(r) for r in rows]:
        print(
            "[diff-endpoint-baseline][err] selftest: render/split round-trip mismatch",
            file=sys.stderr,
        )
        return 1

    # Happy path: identical input must report 0 diff.
    a, r_, c, bi, ci = _diff(rows, rows)
    if (a, r_, c) != ([], [], []):
        print(
            "[diff-endpoint-baseline][err] selftest: identical inputs reported diff",
            file=sys.stderr,
        )
        return 1

    # Negative path: drop one row, ensure it's reported as a removal; mutate
    # one row, ensure it's reported as a change; add one row, ensure it's
    # reported as an addition.
    base = [list(r) for r in rows]
    cur = [list(r) for r in rows]
    cur.pop()  # remove last row
    mutated_idx = 0
    cur[mutated_idx] = list(cur[mutated_idx])
    cur[mutated_idx][2] = "418"  # change STATUSES
    cur.append(["GET", "/__synthetic__", "200", "application/json", "Test", "no"])

    a, r_, c, bi, ci = _diff(base, cur)
    if len(a) != 1 or a[0] != ("GET", "/__synthetic__"):
        print(
            f"[diff-endpoint-baseline][err] selftest: expected 1 addition, got {a!r}",
            file=sys.stderr,
        )
        return 1
    if len(r_) != 1:
        print(
            f"[diff-endpoint-baseline][err] selftest: expected 1 removal, got {r_!r}",
            file=sys.stderr,
        )
        return 1
    if len(c) != 1:
        print(
            f"[diff-endpoint-baseline][err] selftest: expected 1 change, got {c!r}",
            file=sys.stderr,
        )
        return 1

    print(
        "[diff-endpoint-baseline] selftest OK "
        "(round-trip; happy path; +1 / -1 / ~1 on synthetic mutation)."
    )
    return 0


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------


def _main() -> int:
    p = argparse.ArgumentParser(
        description="Diff current OpenAPI endpoint signatures against P0 baseline"
    )
    p.add_argument("--openapi", default=str(DEFAULT_OPENAPI))
    p.add_argument("--baseline", default=str(DEFAULT_BASELINE))
    p.add_argument("--update-baseline", action="store_true")
    p.add_argument("--print", dest="print_only", action="store_true")
    p.add_argument("--selftest", action="store_true")
    args = p.parse_args()

    if args.selftest:
        return _selftest()

    openapi_path = Path(args.openapi)
    if not openapi_path.is_file():
        print(f"[diff-endpoint-baseline][err] OpenAPI not found: {openapi_path}", file=sys.stderr)
        return 2

    doc = parse_endpoints._load_yaml(openapi_path.read_text(encoding="utf-8"))
    if not isinstance(doc, dict):
        print("[diff-endpoint-baseline][err] OpenAPI root is not a mapping", file=sys.stderr)
        return 2

    current_rows = parse_endpoints.extract_endpoints(doc)
    if args.print_only:
        sys.stdout.write(parse_endpoints.render(current_rows))
        return 0

    if args.update_baseline:
        out_path = Path(args.baseline)
        out_path.parent.mkdir(parents=True, exist_ok=True)
        out_path.write_text(
            parse_endpoints.render(current_rows), encoding="utf-8", newline="\n"
        )
        print(
            f"[diff-endpoint-baseline] baseline updated: {out_path} "
            f"({len(current_rows)} rows)"
        )
        return 0

    baseline_path = Path(args.baseline)
    if not baseline_path.is_file():
        print(
            f"[diff-endpoint-baseline][err] baseline not found: {baseline_path}\n"
            f"[diff-endpoint-baseline][err] run `tools/refactor-baseline/capture.{{sh,ps1}} endpoints` first.",
            file=sys.stderr,
        )
        return 2

    baseline_rows = _split_baseline(baseline_path.read_text(encoding="utf-8"))
    additions, removals, changes, base_idx, cur_idx = _diff(baseline_rows, list(map(list, current_rows)))
    return report(additions, removals, changes, base_idx, cur_idx)


if __name__ == "__main__":
    sys.exit(_main())
