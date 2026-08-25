#!/usr/bin/env python3
"""dependency-eol-check: block EOL / known-insecure dependency versions.

Spec: .kiro/specs/fulla-sdk-refactor/tasks.md Task 35 (M6). Parses the
committed ``conan.lock`` (lockfile v0.5) and fails if any locked *runtime*
requirement (``requires`` + ``overrides``) falls below the floor set in the
POLICY table. The policy is deliberately a small, reviewed allow-floor list —
not a live EOL feed — so CI stays deterministic and offline.

Policy entries (extend as the dependency graph grows):

  * openssl  >= 3.0     — the 1.x line is EOL (1.1.1 since 2023-09-11);
                          only 3.x LTS lines receive security fixes.
  * zlib     >= 1.2.13  — CVE-2022-37434 (heap over-read in inflate()).
  * libcurl  >= 8.4.0   — CVE-2023-38545 (SOCKS5 heap buffer overflow).

Lock entries look like ``name/version#revision%timestamp``; only name and
version are inspected. Build tools (``build_requires``) are not enforced:
they never ship in the product and are pinned by Conan anyway.

Usage:
    python tools/security/dependency_eol_check.py [--lockfile <path>]

Exit codes:
    0  no policy violation
    1  at least one dependency is below its security floor
    2  lockfile missing or unparseable
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path
from typing import List, NamedTuple, Optional, Tuple

# --- security floors -------------------------------------------------------
# (package, minimum allowed version, reason shown on violation)
POLICY: List[Tuple[str, str, str]] = [
    ("openssl", "3.0",
     "OpenSSL 1.x is end-of-life (1.1.1 EOL 2023-09-11); only 3.x LTS is supported"),
    ("zlib", "1.2.13",
     "zlib < 1.2.13 is vulnerable to CVE-2022-37434 (inflate() heap over-read)"),
    ("libcurl", "8.4.0",
     "libcurl < 8.4.0 is vulnerable to CVE-2023-38545 (SOCKS5 heap buffer overflow)"),
]

REF_RE = re.compile(r"^([A-Za-z0-9_.+-]+)/([0-9][A-Za-z0-9_.-]*)")


class Violation(NamedTuple):
    package: str
    locked: str
    floor: str
    reason: str


def parse_version(v: str) -> Tuple[int, ...]:
    """Numeric-prefix version tuple: '1.2.13' -> (1, 2, 13); tolerant of
    suffixes like '1.1.1w' (letters beyond the numeric parts are ignored —
    floors in POLICY only ever compare numeric segments)."""
    parts: List[int] = []
    for piece in v.split("."):
        m = re.match(r"(\d+)", piece)
        if not m:
            break
        parts.append(int(m.group(1)))
    return tuple(parts)


def parse_ref(ref: str) -> Optional[Tuple[str, str]]:
    """'openssl/3.5.7#rev%ts' -> ('openssl', '3.5.7'); None if not name/version."""
    m = REF_RE.match(ref)
    return (m.group(1), m.group(2)) if m else None


def collect_requirements(lock: dict) -> List[Tuple[str, str]]:
    refs: List[Tuple[str, str]] = []
    for ref in lock.get("requires", []):
        parsed = parse_ref(ref)
        if parsed:
            refs.append(parsed)
    # overrides map version-ranges to pinned refs; enforce the pins too
    for pins in lock.get("overrides", {}).values():
        for ref in pins:
            parsed = parse_ref(ref)
            if parsed:
                refs.append(parsed)
    return refs


def check(refs: List[Tuple[str, str]]) -> List[Violation]:
    violations: List[Violation] = []
    for name, version in refs:
        for pkg, floor, reason in POLICY:
            if name == pkg and parse_version(version) < parse_version(floor):
                violations.append(Violation(name, version, floor, reason))
    return violations


def main() -> int:
    parser = argparse.ArgumentParser(description="Fulla dependency EOL scan")
    parser.add_argument("--lockfile", default="conan.lock",
                        help="path to conan.lock (default: ./conan.lock)")
    args = parser.parse_args()

    lock_path = Path(args.lockfile)
    if not lock_path.is_file():
        print(f"dependency-eol-check: lockfile not found: {lock_path}", file=sys.stderr)
        return 2
    try:
        lock = json.loads(lock_path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as e:
        print(f"dependency-eol-check: cannot parse {lock_path}: {e}", file=sys.stderr)
        return 2

    refs = collect_requirements(lock)
    if not refs:
        print(f"dependency-eol-check: no requirements found in {lock_path}", file=sys.stderr)
        return 2

    violations = check(refs)
    if violations:
        print(f"dependency-eol-check: {len(violations)} violation(s):\n")
        for v in violations:
            print(f"  {v.package}/{v.locked} < {v.floor}: {v.reason}")
        return 1

    scanned = ", ".join(sorted({name for name, _ in refs}))
    print(f"dependency-eol-check: OK ({len(refs)} locked refs; scanned: {scanned})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
