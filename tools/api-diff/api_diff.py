#!/usr/bin/env python3
"""api-diff: guard the AuthForge SDK public API surface (SemVer enforcement).

Spec: .kiro/specs/authforge-sdk-refactor/tasks.md Task 34 (Wave C), design.md
SS12 toolchain table + SS13 version contract, docs/backend/sdk-runtime-contract.md
SS2. The public API surface is the set of exported SDK headers
(``libs/*/include/authforge/**``, 7 libraries); v1.x promises *source-level*
SemVer on exactly that surface: breaking changes require a major bump.

How the snapshot is built (deterministic, stdlib-only):

  1. Strip C/C++ comments (state machine shared in spirit with arch-guard;
     string/char literals are kept verbatim -- default arguments like an
     issuer URL are part of the API).
  2. Strip function bodies: a ``{`` whose backward scan (skipping identifier
     chars, whitespace, ``::``, template args, ``&*,-[]``) reaches ``)`` is a
     function/ctor body and is replaced by ``;``. Class/enum/namespace bodies
     are kept. Inline *implementation* edits therefore do NOT trip the guard;
     signature/default-arg/include changes DO.
  3. Collapse whitespace, drop blank lines -> the header's "declaration
     skeleton" (a list of lines).

Diff classification against the committed baseline:

  ADDITIVE  new headers, or new skeleton lines in an existing header.
  BREAKING  deleted headers, or removed/changed skeleton lines.

Any drift exits 1 (CI alert). Ratification is explicit via
``--update-baseline``:

  * additive drift  -> ratifiable at the current version;
  * breaking drift  -> ratifiable only when the current major version
    (cmake/Version.cmake) is GREATER than the baseline's recorded major.
    ``--force`` overrides this for changes verified to not affect the
    consumable surface (private members, include reshuffles) -- use in a
    reviewed commit only.

The tool also cross-checks that cmake/Version.cmake, the root CMakeLists.txt
``project(... VERSION x)`` and conanfile.py ``version`` agree (exit 2 drift).

Usage:
    python tools/api-diff/api_diff.py [--root <repo-root>]
        [--baseline <file>] [--update-baseline] [--force] [--print]

Exit codes:
    0  snapshot matches baseline (or baseline update succeeded)
    1  drift detected / breaking update refused
    2  misconfiguration (missing dirs, version drift, unreadable baseline)
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path
from typing import Dict, List, Tuple

# The 7 SDK libraries whose include/ trees form the public API surface.
SDK_LIBS = (
    "common", "oauth2", "identity", "drogon",
    "storage-memory", "storage-postgres", "storage-redis",
)

HEADER_SUFFIXES = {".h", ".hpp", ".hh", ".hxx"}

# Characters skipped while scanning backwards from '{' to decide whether it
# opens a function body (see module docstring, step 2).
_BACKSCAN_SKIP = set(
    "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_"
    " \t\n\r<>,&*-[]."
)


def strip_comments(source: str) -> str:
    """Blank out C/C++ comments, preserving layout; literals kept verbatim."""
    out: List[str] = []
    i, n = 0, len(source)
    state = "code"  # code | line_comment | block_comment | string | char
    while i < n:
        ch = source[i]
        nxt = source[i + 1] if i + 1 < n else ""
        if state == "code":
            if ch == "/" and nxt == "/":
                out.append("  ")
                i += 2
                state = "line_comment"
                continue
            if ch == "/" and nxt == "*":
                out.append("  ")
                i += 2
                state = "block_comment"
                continue
            if ch == '"':
                out.append(ch)
                i += 1
                state = "string"
                continue
            if ch == "'":
                out.append(ch)
                i += 1
                state = "char"
                continue
            out.append(ch)
            i += 1
            continue
        if state == "line_comment":
            if ch == "\n":
                out.append("\n")
                state = "code"
            else:
                out.append(" ")
            i += 1
            continue
        if state == "block_comment":
            if ch == "*" and nxt == "/":
                out.append("  ")
                i += 2
                state = "code"
                continue
            out.append("\n" if ch == "\n" else " ")
            i += 1
            continue
        # string / char literal: copy verbatim, honoring escapes.
        out.append(ch)
        if ch == "\\" and nxt:
            out.append(nxt)
            i += 2
            continue
        if (state == "string" and ch == '"') or (state == "char" and ch == "'"):
            state = "code"
        i += 1
    return "".join(out)


def _is_function_body(text: str, brace_pos: int) -> bool:
    """Backward scan from a '{': skipping trivia, does it reach a ')'?

    ')' means qualifiers/trailing-return of a function signature (or a ctor
    init list) precede the brace -> body. Anything else (identifier run to
    ';'/'}'/'{', an '=', a lone ':' of an enum base) -> keep the block.
    """
    i = brace_pos - 1
    while i >= 0:
        ch = text[i]
        if ch == ":":
            if i > 0 and text[i - 1] == ":":  # '::' scope -- skip both
                i -= 2
                continue
            return False  # enum base / label
        if ch in _BACKSCAN_SKIP:
            i -= 1
            continue
        return ch == ")"
    return False


def strip_bodies(text: str) -> str:
    """Replace every function body ``{...}`` with ``;`` (comment-free input)."""
    out: List[str] = []
    i, n = 0, len(text)
    while i < n:
        ch = text[i]
        if ch in "\"'":  # skip literals wholesale
            quote = ch
            out.append(ch)
            i += 1
            while i < n:
                out.append(text[i])
                if text[i] == "\\" and i + 1 < n:
                    out.append(text[i + 1])
                    i += 2
                    continue
                if text[i] == quote:
                    i += 1
                    break
                i += 1
            continue
        if ch == "{" and _is_function_body(text, i):
            # Consume the balanced block (literal-aware), emit ';'.
            depth = 1
            i += 1
            while i < n and depth:
                c = text[i]
                if c in "\"'":
                    quote = c
                    i += 1
                    while i < n:
                        if text[i] == "\\" and i + 1 < n:
                            i += 2
                            continue
                        if text[i] == quote:
                            i += 1
                            break
                        i += 1
                    continue
                if c == "{":
                    depth += 1
                elif c == "}":
                    depth -= 1
                i += 1
            out.append(";")
            continue
        out.append(ch)
        i += 1
    return "".join(out)


def skeleton(source: str) -> List[str]:
    """Comment-free, body-free, whitespace-normalized declaration lines."""
    text = strip_bodies(strip_comments(source))
    lines: List[str] = []
    for raw in text.splitlines():
        line = re.sub(r"\s+", " ", raw).strip()
        if line:
            lines.append(line)
    return lines


# --- snapshot ---------------------------------------------------------------

def collect_headers(root: Path) -> List[Path]:
    headers: List[Path] = []
    for lib in SDK_LIBS:
        inc = root / "libs" / lib / "include"
        if not inc.is_dir():
            raise FileNotFoundError(f"missing SDK include tree: {inc}")
        headers.extend(
            p for p in inc.rglob("*") if p.suffix in HEADER_SUFFIXES and p.is_file()
        )
    return sorted(headers, key=lambda p: p.as_posix())


def build_snapshot(root: Path) -> Dict[str, List[str]]:
    snap: Dict[str, List[str]] = {}
    for path in collect_headers(root):
        rel = path.relative_to(root).as_posix()
        snap[rel] = skeleton(path.read_text(encoding="utf-8", errors="replace"))
    return snap


# --- baseline I/O -----------------------------------------------------------

HEADER_MARK = "=== "


def render_baseline(snap: Dict[str, List[str]], version: str) -> str:
    # Metadata lines use '//' -- skeleton lines are comment-stripped C++, so
    # they can never start with '//' ('#include'/'#pragma' DO start with '#').
    parts = [
        "// api-diff baseline -- AuthForge SDK public header skeletons.",
        "// Regenerate ONLY via: python tools/api-diff/api_diff.py --update-baseline",
        f"// version: {version}",
    ]
    for rel in sorted(snap):
        parts.append(HEADER_MARK + rel)
        parts.extend(snap[rel])
    return "\n".join(parts) + "\n"


def parse_baseline(text: str) -> Tuple[Dict[str, List[str]], str]:
    snap: Dict[str, List[str]] = {}
    version = ""
    current: List[str] = []
    for line in text.splitlines():
        if line.startswith("// version:"):
            version = line.split(":", 1)[1].strip()
            continue
        if line.startswith("//"):
            continue
        if line.startswith(HEADER_MARK):
            current = snap.setdefault(line[len(HEADER_MARK):].strip(), [])
            continue
        current.append(line)
    return snap, version


# --- version handling -------------------------------------------------------

def read_versions(root: Path) -> str:
    """Return the repo version; exit 2 if the three declarations drift."""
    vc = (root / "cmake" / "Version.cmake").read_text(encoding="utf-8")
    parts = {}
    for field in ("MAJOR", "MINOR", "PATCH"):
        m = re.search(rf"OAUTH2_PROJECT_VERSION_{field}\s+(\d+)", vc)
        if not m:
            print(f"api-diff: cannot parse {field} from cmake/Version.cmake",
                  file=sys.stderr)
            sys.exit(2)
        parts[field] = m.group(1)
    version = "{MAJOR}.{MINOR}.{PATCH}".format(**parts)

    cm = (root / "CMakeLists.txt").read_text(encoding="utf-8")
    m = re.search(r"project\(\s*\S+\s+VERSION\s+([\d.]+)", cm)
    cmake_ver = m.group(1) if m else "?"
    cf = (root / "conanfile.py").read_text(encoding="utf-8")
    m = re.search(r'version\s*=\s*"([\d.]+)"', cf)
    conan_ver = m.group(1) if m else "?"
    if not (version == cmake_ver == conan_ver):
        print("api-diff: version drift -- cmake/Version.cmake="
              f"{version}, CMakeLists.txt={cmake_ver}, conanfile.py={conan_ver}",
              file=sys.stderr)
        sys.exit(2)
    return version


def major_of(version: str) -> int:
    return int(version.split(".", 1)[0]) if version else 0


# --- diff -------------------------------------------------------------------

class Drift:
    def __init__(self) -> None:
        self.added_headers: List[str] = []
        self.removed_headers: List[str] = []
        self.added_lines: Dict[str, List[str]] = {}
        self.removed_lines: Dict[str, List[str]] = {}

    @property
    def breaking(self) -> bool:
        return bool(self.removed_headers or self.removed_lines)

    @property
    def any(self) -> bool:
        return bool(self.added_headers or self.removed_headers
                    or self.added_lines or self.removed_lines)


def _multiset_diff(old: List[str], new: List[str]) -> Tuple[List[str], List[str]]:
    """Order-insensitive removed/added lines (multiset semantics)."""
    from collections import Counter
    old_c, new_c = Counter(old), Counter(new)
    removed = sorted((old_c - new_c).elements())
    added = sorted((new_c - old_c).elements())
    return removed, added


# A declaration parameter line carrying a default value, e.g.
# "      int doubleDeleteDelayMs = 0" (optionally with a trailing comma when
# it is no longer the last parameter). Comparison operators and full
# definitions (with ';') are excluded via the [^=;] value charset.
_DEFAULTED_PARAM_RE = re.compile(r'^\s*[\w:<>,\s*&.]+\s+\w+\s*=\s*[^=;]+,?\s*$')


def _strip_close_paren(line: str) -> str:
    """Drop a trailing ');' from a declaration's last-parameter line so both
    brace styles (close-paren on its own line vs. sharing the parameter line)
    normalize to the same shape."""
    s = line.rstrip()
    return s[:-2] if s.endswith(');') else s


def _extract_defaulted_param_appends(removed: List[str],
                                     added: List[str]) -> Tuple[List[str], List[str], int]:
    """#89: reclassify line changes that are actually trailing defaulted-
    parameter appends on an existing declaration.

    Shape (as produced by _multiset_diff): the old LAST parameter line
    ``int x = 60`` (or ``int x = 60);`` when the close-paren shares the line)
    disappears while the comma-terminated form plus one or more NEW defaulted
    parameters appear. Appending parameters that all carry defaults is
    semantically ADDITIVE (existing call sites compile unchanged), so the
    paired old line must not count as BREAKING. A non-defaulted append WOULD
    break callers and is deliberately left in ``removed``.

    Conservative pairing rule: for each removed line R, R's comma form
    (close-paren stripped, ',' appended) must be present in ``added`` AND
    every co-added line at R's indentation that looks like a parameter must
    match the defaulted shape. Returns (removed, added, pair_count) with the
    comma forms dropped from ``added``.
    """
    added_set = set(added)
    pair_count = 0
    kept_removed: List[str] = []
    consumed: set = set()
    for r in removed:
        base = _strip_close_paren(r)
        comma_form = base.rstrip().rstrip(',') + ','
        if base.endswith(',') or comma_form not in added_set:
            kept_removed.append(r)
            continue
        indent = r[:len(r) - len(r.lstrip())]
        param_shaped = [a for a in added_set
                        if a != comma_form and a not in consumed
                        and a.startswith(indent)
                        and not a.strip().startswith(('}', ')', '{'))]
        shaped_ok = all(_DEFAULTED_PARAM_RE.match(_strip_close_paren(a))
                        for a in param_shaped)
        if not param_shaped or not shaped_ok:
            # No co-added parameters, or at least one append lacks a default
            # (a caller-breaking change) — keep R as BREAKING.
            kept_removed.append(r)
            continue
        # Additive append: pair R <-> comma form; the co-added defaulted
        # params stay in `added` (they are the ADDITIONS).
        pair_count += 1
        consumed.add(comma_form)
    if not pair_count:
        return removed, added, 0
    new_added = [a for a in added if a not in consumed]
    return kept_removed, new_added, pair_count


def diff_snapshots(base: Dict[str, List[str]], cur: Dict[str, List[str]]) -> Drift:
    d = Drift()
    d.added_headers = sorted(set(cur) - set(base))
    d.removed_headers = sorted(set(base) - set(cur))
    for rel in sorted(set(base) & set(cur)):
        removed, added = _multiset_diff(base[rel], cur[rel])
        removed, added, pairs = _extract_defaulted_param_appends(removed, added)
        if pairs:
            print(f"  [ADDITIVE] {rel}: {pairs} trailing defaulted-parameter "
                  f"append(s) recognized (old last-param line gained a comma; "
                  f"all appended parameters carry defaults — existing call "
                  f"sites unchanged)")
        if removed:
            d.removed_lines[rel] = removed
        if added:
            d.added_lines[rel] = added
    return d


def report(d: Drift, baseline_version: str, current_version: str) -> None:
    print("api-diff: drift against baseline "
          f"(baselined at v{baseline_version}, current v{current_version}):\n")
    for rel in d.added_headers:
        print(f"  [ADDITIVE] new header: {rel}")
    for rel, lines in d.added_lines.items():
        print(f"  [ADDITIVE] {rel}: {len(lines)} new declaration line(s)")
        for line in lines[:5]:
            print(f"      + {line}")
    for rel in d.removed_headers:
        print(f"  [BREAKING] header removed: {rel}")
    for rel, lines in d.removed_lines.items():
        print(f"  [BREAKING] {rel}: {len(lines)} removed/changed line(s)")
        for line in lines[:5]:
            print(f"      - {line}")
    print("")
    if d.breaking:
        print("Breaking drift: removed/changed declarations require a MAJOR")
        print("version bump (design.md SS13). Bump cmake/Version.cmake (+ root")
        print("CMakeLists.txt, conanfile.py), then run --update-baseline.")
    else:
        print("Additive drift: review the new API, then ratify with")
        print("--update-baseline (no version bump required for additions).")


def main(argv: List[str]) -> int:
    parser = argparse.ArgumentParser(description="AuthForge SDK API surface guard")
    default_root = Path(__file__).resolve().parents[2]
    parser.add_argument("--root", type=Path, default=default_root,
                        help="repository root (default: two levels above this script)")
    parser.add_argument("--baseline", type=Path, default=None,
                        help="baseline file (default: api-baseline.txt beside this script)")
    parser.add_argument("--update-baseline", action="store_true",
                        help="ratify the current API surface as the new baseline")
    parser.add_argument("--force", action="store_true",
                        help="allow --update-baseline for breaking drift WITHOUT a major "
                             "bump (only for verified non-public-surface changes, e.g. "
                             "private members; use in a reviewed commit)")
    parser.add_argument("--print", dest="print_only", action="store_true",
                        help="print the current snapshot and exit")
    args = parser.parse_args(argv)

    root: Path = args.root.resolve()
    baseline_path = (args.baseline if args.baseline
                     else Path(__file__).resolve().parent / "api-baseline.txt")

    try:
        current = build_snapshot(root)
    except FileNotFoundError as exc:
        print(f"api-diff: {exc}", file=sys.stderr)
        return 2
    version = read_versions(root)

    if args.print_only:
        sys.stdout.write(render_baseline(current, version))
        return 0

    if not baseline_path.is_file():
        if args.update_baseline:
            baseline_path.write_text(render_baseline(current, version),
                                     encoding="utf-8", newline="\n")
            print(f"api-diff: baseline created ({len(current)} headers, "
                  f"v{version}) -> {baseline_path}")
            return 0
        print(f"api-diff: baseline missing: {baseline_path}; "
              "run with --update-baseline and commit it", file=sys.stderr)
        return 1

    base, base_version = parse_baseline(
        baseline_path.read_text(encoding="utf-8"))
    drift = diff_snapshots(base, current)

    if args.update_baseline:
        if drift.breaking and major_of(version) <= major_of(base_version) \
                and not args.force:
            report(drift, base_version, version)
            print("api-diff: REFUSED -- breaking drift needs current major "
                  f"({major_of(version)}) > baseline major ({major_of(base_version)}), "
                  "or --force for verified non-public-surface changes.")
            return 1
        baseline_path.write_text(render_baseline(current, version),
                                 encoding="utf-8", newline="\n")
        print(f"api-diff: baseline updated ({len(current)} headers, "
              f"v{version}) -> {baseline_path}")
        return 0

    if drift.any:
        report(drift, base_version, version)
        return 1

    print(f"api-diff: OK ({len(current)} headers match baseline, "
          f"v{version}, baselined at v{base_version})")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
