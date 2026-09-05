#!/usr/bin/env python3
"""Aggregate line coverage from gcov JSON output across libs/*/src/*.cc.

Per gcc docs: `gcov -j <src>.gcda` writes `<src>.gcov.json.gz` next to the
.gcda. Each file entry has {file, functions, lines[]} where lines[] entries
have {count, unexecuted_block}. A line is "covered" when count > 0.

Usage: find <build>/libs -path "*/src/*" -name "*.gcov.json.gz" | \
       python3 scripts/measure_coverage.py [--json] [--ratchet BASELINE.json]

Output modes:
  (default)  human-readable per-library / per-file text report
  --json     machine-readable per-library JSON instead of the text report:
             {"libs": {"<name>": {"covered": C, "total": T, "pct": P}}, ...}
  --ratchet PATH
             no-decrease gate against a committed baseline JSON (issue #105,
             decision B): FAIL (exit 1) when a library's coverage drops more
             than RATCHET_TOLERANCE_PP below its baseline. Anti-flake rules:
               * compare trunc-to-0.1pp(current) vs baseline - tolerance;
               * libraries with fewer than MIN_LINES instrumented lines are
                 exempt (timing-dependent tests flap a few lines, which a
                 0.5pp band absorbs on 1k+ line libs but not on tiny ones);
               * a baseline lib missing from the current run FAILS (lost
                 instrumentation is never a pass);
               * a NEW lib (not in baseline) passes (baseline catches it on
                 the next deliberate --update run).
             When the baseline file does not exist, the gate runs in SEED
             mode: it prints the baseline JSON to stdout and exits 0, so the
             numbers can be copied into tools/coverage/ratchet-baseline.json
             and committed.

Re-baselining (coverage improvements) is a deliberate PR that edits the
baseline JSON; there is no automatic upward ratchet.
"""
import sys, json, gzip, os, re, argparse
from collections import defaultdict

cov = defaultdict(int)
tot = defaultdict(int)
perfile = {}
seen = set()

SRC_RE = re.compile(r".*/(libs/[^/]+/src/.+\.cc)$")
LIB_RE = re.compile(r"libs/([^/]+)/")

# Anti-flake constants (see module docstring).
RATCHET_TOLERANCE_PP = 0.5
MIN_LINES = 200

for line in sys.stdin:
    p = line.strip().rstrip("\0")
    if not p or p in seen:
        continue
    seen.add(p)
    try:
        d = json.load(gzip.open(p, "rt"))
    except Exception:
        continue
    for f in d.get("files", []):
        fn = f.get("file", "")
        m = SRC_RE.match(fn)
        if not m or "/models/" in fn:
            continue
        rel = m.group(1)
        libm = LIB_RE.match(rel)
        if not libm:
            continue
        lib = libm.group(1)
        lines = f.get("lines", [])
        covered = sum(1 for l in lines if l["count"] > 0)
        total = len(lines)
        if total == 0:
            continue
        cov[lib] += covered
        tot[lib] += total
        perfile[rel] = (covered, total)


def lib_pct(lib):
    return round(cov[lib] * 100.0 / tot[lib], 1)


def text_report():
    print("=== per-library line coverage (libs/<name>/src/*.cc, models excluded) ===")
    for lib in sorted(tot):
        print(f"  {lib:22} {cov[lib]:6d} / {tot[lib]:<6d}  {cov[lib]*100.0/tot[lib]:5.1f}%")
    tc = sum(cov.values())
    tt = sum(tot.values())
    if tt == 0:
        print("\n  OVERALL libs/: no coverage data (0 instrumented lines found)")
    else:
        print(f"\n  {'OVERALL libs/':22} {tc:6d} / {tt:<6d}  {tc*100.0/tt:5.1f}%")

    print("\n=== admin service per-file (was 0% in baseline) ===")
    for rel in sorted(perfile):
        if "/admin/" in rel:
            c, t = perfile[rel]
            print(f"  {os.path.basename(rel):32} {c:4d} / {t:<4d}  {c*100.0/t:5.1f}%")

    print("\n=== controllers per-file ===")
    for rel in sorted(perfile):
        if "/controllers/" in rel:
            c, t = perfile[rel]
            print(f"  {os.path.basename(rel):36} {c:4d} / {t:<4d}  {c*100.0/t:5.1f}%")


def baseline_json():
    return {
        "libs": {
            lib: {"covered": cov[lib], "total": tot[lib], "pct": lib_pct(lib)}
            for lib in sorted(tot)
        }
    }


def ratchet_gate(path):
    if not os.path.exists(path):
        print(f"[ratchet] SEED MODE: baseline {path} not found.")
        print("[ratchet] Commit the following as the baseline (no gate this run):")
        print(json.dumps(baseline_json(), indent=2))
        return 0
    with open(path, "r", encoding="utf-8") as fh:
        base = json.load(fh).get("libs", {})
    violations = []
    notes = []
    for lib, entry in sorted(base.items()):
        if lib not in tot:
            violations.append(
                f"[ratchet] FAIL {lib}: present in baseline but NO coverage data "
                f"in this run (lost instrumentation?)"
            )
            continue
        base_pct = float(entry["pct"])
        if tot[lib] < MIN_LINES:
            notes.append(
                f"[ratchet] exempt {lib}: {tot[lib]} instrumented lines < {MIN_LINES}"
            )
            continue
        # trunc-to-0.1pp of the current run vs baseline minus tolerance
        current = int(lib_pct(lib) * 10) / 10.0
        floor = round(base_pct - RATCHET_TOLERANCE_PP, 1)
        if current < floor:
            violations.append(
                f"[ratchet] FAIL {lib}: {current:.1f}% < baseline {base_pct:.1f}% "
                f"- {RATCHET_TOLERANCE_PP:.1f}pp tolerance"
            )
    for lib in sorted(tot):
        if lib not in base:
            notes.append(f"[ratchet] new lib {lib}: not in baseline (passes this run)")
    for n in notes:
        print(n)
    if violations:
        for v in violations:
            print(v)
        print(
            "[ratchet] coverage regressed — restore the drop or re-baseline "
            "deliberately (edit the JSON in a reviewed PR)"
        )
        return 1
    print(f"[ratchet] OK: no library dropped more than {RATCHET_TOLERANCE_PP}pp below baseline")
    return 0


def main():
    ap = argparse.ArgumentParser(add_help=True)
    ap.add_argument("--json", action="store_true", help="emit per-lib JSON instead of text")
    ap.add_argument(
        "--ratchet",
        metavar="BASELINE.json",
        default=None,
        help="no-decrease gate against a committed baseline (seed mode if absent)",
    )
    args = ap.parse_args()
    if args.json:
        print(json.dumps(baseline_json(), indent=2))
        return 0
    text_report()
    if args.ratchet:
        return ratchet_gate(args.ratchet)
    return 0


if __name__ == "__main__":
    sys.exit(main())
