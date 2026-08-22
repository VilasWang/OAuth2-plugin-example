#!/usr/bin/env python3
"""drogon_macro_bool_check.py -- flag bare ``||``/``&&`` inside drogon CHECK()/REQUIRE().

Why (#76): drogon_test.h's CHECK/REQUIRE are macros, not functions.
``CHECK_INTERNAL__`` expands the expression via
``(drogon::test::internal::Decomposer() <= expr)`` and macro argument
substitution does not parenthesize, so an unparenthesized boolean chain
``a || b`` re-associates to ``(Decomposer() <= a) || b`` -- which silently
mis-evaluates and looks like a flaky assertion. Real instance: PR #68
debugged ``CHECK(body.isMember("error") || body.isMember("code"))`` failing
although the raw body demonstrably contained the key.

Safe forms (not flagged):
  CHECK((bool)(a || b))     -- the repo's established precedent
                               (tests/e2e-backend/oauth2_flows/FunctionalTest.cc)
  CHECK((a || b))           -- outer parens bind the chain to <=
  CHECK(f(a || b))          -- operator nested inside call/index brackets
  CHECK(a == (b || c))      -- parenthesized sub-expression

Usage:
  drogon_macro_bool_check.py [--selftest] [DIR ...]   (default: tests)

Exit codes:
  0  no violations (or selftest passed)
  1  violations found (file:line listed) or selftest failed
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

MACRO_START = re.compile(r"\b(CHECK|REQUIRE)\s*\(")

# Embedded selftest samples: (line, expect_violation).
SELFTEST_CASES = [
    ("CHECK(a || b);", True),
    ("CHECK( x != npos || y != npos );", True),
    ("REQUIRE(ok || fallback);", True),
    ("CHECK(cond && other);", True),
    # multi-line, operator on a later line at macro-argument depth
    ("CHECK(a\n       || b);", True),
    ("CHECK((bool)(a || b));", False),
    ("CHECK((a || b));", False),
    ("CHECK(f(a || b));", False),
    ("CHECK(x == (a || b));", False),
    ("CHECK(a);", False),
    ("CHECK_THROWS(f(a || b));", False),  # THROWS variants use EVAL__, not Decomposer
    ("REQUIRE_THROWS(g() || die());", False),
    ('CHECK(msg == "a || b");', False),  # operator inside a string literal
    ("// CHECK(a || b) commented out", False),
]


def blank_literals_and_comments(text: str) -> str:
    """Replace string/char literal and comment CONTENT with spaces.

    Keeps newlines and total length so column/line math stays valid.
    """
    out = list(text)
    i, n = 0, len(text)
    state = "code"
    while i < n:
        c = text[i]
        nxt = text[i + 1] if i + 1 < n else ""
        if state == "code":
            if c == "/" and nxt == "/":
                state = "line"
                out[i] = out[i + 1] = " "
                i += 2
                continue
            if c == "/" and nxt == "*":
                state = "block"
                out[i] = out[i + 1] = " "
                i += 2
                continue
            if c == '"':
                state = "str"
                out[i] = " "
                i += 1
                continue
            if c == "'":
                state = "chr"
                out[i] = " "
                i += 1
                continue
        elif state == "line":
            if c == "\n":
                state = "code"
            else:
                out[i] = " "
        elif state == "block":
            if c == "*" and nxt == "/":
                out[i] = out[i + 1] = " "
                state = "code"
                i += 2
                continue
            if c != "\n":
                out[i] = " "
        elif state in ("str", "chr"):
            if c == "\\" and nxt:
                out[i] = " "
                out[i + 1] = " "
                i += 2
                continue
            if (state == "str" and c == '"') or (state == "chr" and c == "'"):
                out[i] = " "
                state = "code"
            elif c != "\n":
                out[i] = " "
        i += 1
    return "".join(out)


def scan_text(blanked: str) -> list[int]:
    """Return 0-based line numbers of unsafe boolean operands."""
    hits: list[int] = []
    for m in MACRO_START.finditer(blanked):
        i = m.end()  # first char after the opening paren
        depth = 1
        n = len(blanked)
        while i < n and depth > 0:
            c = blanked[i]
            if c == "(":
                depth += 1
            elif c == ")":
                depth -= 1
            elif depth == 1 and c in "|&" and i + 1 < n and blanked[i + 1] == c:
                hits.append(blanked.count("\n", 0, i))
                break  # one report per macro invocation is enough
            i += 1
    return hits


def scan_file(path: Path) -> list[int]:
    try:
        text = path.read_text(encoding="utf-8", errors="replace")
    except OSError as e:
        print(f"[warn] unreadable: {path} ({e})", file=sys.stderr)
        return []
    return scan_text(blank_literals_and_comments(text))


def iter_sources(dirs: list[str]):
    for d in dirs:
        root = Path(d)
        if root.is_file():
            yield root
            continue
        for p in sorted(root.rglob("*")):
            if p.suffix in (".cc", ".cpp", ".cxx", ".h", ".hpp"):
                yield p


def run_selftest() -> int:
    failed = 0
    for line, expect in SELFTEST_CASES:
        got = bool(scan_text(blank_literals_and_comments(line)))
        if got != expect:
            failed += 1
            print(f"[selftest] MISMATCH ({'flag' if got else 'clean'}, expected "
                  f"{'flag' if expect else 'clean'}): {line!r}")
    if failed:
        print(f"[drogon-macro-bool] selftest FAILED ({failed} mismatches)")
        return 1
    print(f"[drogon-macro-bool] selftest OK ({len(SELFTEST_CASES)} cases)")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("dirs", nargs="*", default=["tests"], help="dirs/files to scan")
    ap.add_argument("--selftest", action="store_true", help="run embedded samples")
    args = ap.parse_args()

    if args.selftest:
        return run_selftest()

    violations = 0
    scanned = 0
    for p in iter_sources(args.dirs):
        scanned += 1
        for line0 in scan_file(p):
            violations += 1
            print(f"{p}:{line0 + 1}: bare `||`/`&&` inside CHECK/REQUIRE -- wrap in "
                  f"`(bool)(...)` or split the assertion (drogon Decomposer re-association)")
    if violations:
        print(f"[drogon-macro-bool] FAIL: {violations} unsafe assertion(s) in {scanned} file(s)")
        return 1
    print(f"[drogon-macro-bool] OK: {scanned} file(s) scanned, no unsafe boolean operands")
    return 0


if __name__ == "__main__":
    sys.exit(main())
