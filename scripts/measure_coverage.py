#!/usr/bin/env python3
"""Aggregate line coverage from gcov JSON output across libs/*/src/*.cc.

Per gcc docs: `gcov -j <src>.gcda` writes `<src>.gcov.json.gz` next to the
.gcda. Each file entry has {file, functions, lines[]} where lines[] entries
have {count, unexecuted_block}. A line is "covered" when count > 0.

Usage: find <build>/libs -path "*/src/*" -name "*.gcov.json.gz" | \
       python3 scripts/measure_coverage.py
"""
import sys, json, gzip, os, re
from collections import defaultdict

cov = defaultdict(int)
tot = defaultdict(int)
perfile = {}
seen = set()

SRC_RE = re.compile(r".*/(libs/[^/]+/src/.+\.cc)$")
LIB_RE = re.compile(r"libs/([^/]+)/")

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
