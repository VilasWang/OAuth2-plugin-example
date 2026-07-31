#!/usr/bin/env python3
"""Extract HTTP endpoint signatures from an OpenAPI 3.x document.

This is the **scaffolding-level** P0 endpoint baseline collector (see
tasks.md task 1.4, scheme B): it captures the static contract surface from
`apps/server/openapi.yaml` so that `tools/diff-endpoint-baseline.py` (task
1.5) has a structural target to diff against. Live response capture
(status, headers, JSON body shape per scenario) will be backfilled in P7
once docker compose is reorganised and the smoke gate is reachable.

Output format (one record per line, sorted by path then method):

    <METHOD>\t<PATH>\t<EXPECTED_STATUSES>\t<RESPONSE_CONTENT_TYPES>\t<TAGS>\t<SECURITY>

  - METHOD                : uppercase, e.g. GET / POST / PUT / DELETE / PATCH.
  - PATH                  : verbatim from `paths`.
  - EXPECTED_STATUSES     : comma-separated list of declared response codes,
                            sorted ascending, e.g. "200,400,401".
  - RESPONSE_CONTENT_TYPES: comma-separated unique content-types declared
                            across all responses, sorted, e.g.
                            "application/json,text/html". "-" if none.
  - TAGS                  : comma-separated tag list, sorted, "-" if none.
  - SECURITY              : "yes" if the operation declares any non-empty
                            security requirement, "no" otherwise.

The format is intentionally strict-ASCII tab-separated so it diffs cleanly
across phases and platforms regardless of console encoding.

Usage:
    parse_endpoints.py --openapi <path>             --out <out.txt>
    parse_endpoints.py --openapi <path>             --print
    parse_endpoints.py --selftest

The YAML loader uses PyYAML if available, otherwise falls back to a tiny
hand-rolled OpenAPI subset parser sufficient for the shape produced by
`apps/server/openapi.yaml`. This avoids forcing every CI runner to
install PyYAML just for the P0 scaffolding step.

Spec references:
    _Design: §2.8 P0, Property 2_
    _Requirements: 14.1, 15.1, 15.5, 15.6, 15.11_
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path
from typing import Dict, List, Tuple

_HTTP_METHODS = ("get", "post", "put", "delete", "patch", "options", "head", "trace")


# ---------------------------------------------------------------------------
# YAML loader: prefer PyYAML, fall back to a minimal indentation-based parser.
# ---------------------------------------------------------------------------


def _load_yaml(text: str):
    try:
        import yaml  # type: ignore

        return yaml.safe_load(text)
    except ImportError:
        pass
    # Fallback: a tiny YAML subset parser supporting nested mappings,
    # `[a, b]` flow sequences, and `- item` block sequences. Sufficient for
    # OpenAPI 3.x emitted by the project's openapi.yaml.
    return _MiniYaml(text).parse()


class _MiniYaml:
    _SCALAR_RE = re.compile(r"^(?P<key>[^:#]+):\s*(?P<val>.*?)\s*$")
    _FLOW_SEQ_RE = re.compile(r"^\[(.*)\]$")

    def __init__(self, text: str):
        self.lines = [
            ln.rstrip()
            for ln in text.splitlines()
            if ln.strip() and not ln.lstrip().startswith("#")
        ]
        self.idx = 0

    @staticmethod
    def _coerce(scalar: str):
        s = scalar.strip()
        if s == "":
            return None
        if s.lower() in ("true", "false"):
            return s.lower() == "true"
        if s.lower() == "null" or s == "~":
            return None
        # Strip surrounding quotes if balanced.
        if (s.startswith('"') and s.endswith('"')) or (
            s.startswith("'") and s.endswith("'")
        ):
            return s[1:-1]
        if re.fullmatch(r"-?\d+", s):
            return int(s)
        return s

    @classmethod
    def _flow_seq(cls, raw: str):
        items_raw = raw.strip()
        m = cls._FLOW_SEQ_RE.match(items_raw)
        if not m:
            return None
        body = m.group(1).strip()
        if not body:
            return []
        return [cls._coerce(p) for p in re.split(r"\s*,\s*", body)]

    @staticmethod
    def _indent(line: str) -> int:
        return len(line) - len(line.lstrip(" "))

    def parse(self):
        return self._parse_node(0)

    def _peek(self):
        return self.lines[self.idx] if self.idx < len(self.lines) else None

    def _parse_node(self, indent: int):
        line = self._peek()
        if line is None:
            return None
        cur_indent = self._indent(line)
        if cur_indent < indent:
            return None
        body = line[cur_indent:]
        if body.startswith("- "):
            return self._parse_seq(cur_indent)
        return self._parse_map(cur_indent)

    def _parse_map(self, indent: int):
        result: Dict = {}
        while True:
            line = self._peek()
            if line is None:
                break
            cur_indent = self._indent(line)
            if cur_indent < indent:
                break
            if cur_indent > indent:
                # Should not happen at this level; bail to outer parser.
                break
            body = line[cur_indent:]
            if body.startswith("- "):
                # We're inside a sequence; the caller will pick it up.
                break
            m = self._SCALAR_RE.match(body)
            if not m:
                # Skip unparseable line conservatively.
                self.idx += 1
                continue
            key = m.group("key").strip()
            val = m.group("val")
            self.idx += 1
            if val == "":
                # Nested block; child indent must be greater.
                child = self._parse_node(indent + 2)
                if child is None:
                    # Could be empty mapping, which we represent as {}.
                    result[key] = {}
                else:
                    result[key] = child
                continue
            seq = self._flow_seq(val)
            if seq is not None:
                result[key] = seq
                continue
            result[key] = self._coerce(val)
        return result

    def _parse_seq(self, indent: int):
        items: List = []
        while True:
            line = self._peek()
            if line is None:
                break
            cur_indent = self._indent(line)
            if cur_indent < indent:
                break
            body = line[cur_indent:]
            if not body.startswith("- "):
                break
            inline = body[2:]
            self.idx += 1
            if inline.strip() == "":
                child = self._parse_node(indent + 2)
                items.append(child)
                continue
            # Inline scalar or inline mapping like "- key: value".
            m = self._SCALAR_RE.match(inline)
            if m and m.group("val") != "":
                # `- key: value` → start of a mapping element. Combine
                # with any nested keys at indent + 2.
                first_key = m.group("key").strip()
                first_val = m.group("val")
                seq = self._flow_seq(first_val)
                if seq is not None:
                    item = {first_key: seq}
                else:
                    item = {first_key: self._coerce(first_val)}
                # Greedily absorb following indented mapping lines as part
                # of the same item.
                while True:
                    nxt = self._peek()
                    if nxt is None:
                        break
                    nxt_indent = self._indent(nxt)
                    if nxt_indent <= indent:
                        break
                    nxt_body = nxt[nxt_indent:]
                    if nxt_body.startswith("- "):
                        break
                    m2 = self._SCALAR_RE.match(nxt_body)
                    if not m2:
                        break
                    k2 = m2.group("key").strip()
                    v2 = m2.group("val")
                    self.idx += 1
                    if v2 == "":
                        item[k2] = self._parse_node(nxt_indent + 2) or {}
                    else:
                        seq2 = self._flow_seq(v2)
                        item[k2] = seq2 if seq2 is not None else self._coerce(v2)
                items.append(item)
                continue
            items.append(self._coerce(inline))
        return items


# ---------------------------------------------------------------------------
# Endpoint extractor
# ---------------------------------------------------------------------------


def extract_endpoints(doc: dict) -> List[Tuple[str, str, str, str, str, str]]:
    paths = (doc or {}).get("paths") or {}
    if not isinstance(paths, dict):
        return []
    rows: List[Tuple[str, str, str, str, str, str]] = []
    for path, item in sorted(paths.items()):
        if not isinstance(item, dict):
            continue
        for method in _HTTP_METHODS:
            op = item.get(method)
            if not isinstance(op, dict):
                continue
            statuses, content_types = _summarise_responses(op.get("responses"))
            tags = op.get("tags") or []
            tag_str = ",".join(sorted({str(t) for t in tags})) if tags else "-"
            sec = op.get("security")
            sec_str = "yes" if _has_security(sec) else "no"
            rows.append(
                (
                    method.upper(),
                    str(path),
                    statuses or "-",
                    content_types or "-",
                    tag_str,
                    sec_str,
                )
            )
    return rows


def _summarise_responses(responses) -> Tuple[str, str]:
    if not isinstance(responses, dict):
        return "-", "-"
    statuses: List[str] = []
    cts: set = set()
    for code, body in responses.items():
        statuses.append(str(code))
        if isinstance(body, dict):
            content = body.get("content")
            if isinstance(content, dict):
                for ct in content.keys():
                    cts.add(str(ct))
    statuses_sorted = sorted(statuses, key=lambda s: (len(s), s))
    return ",".join(statuses_sorted), (",".join(sorted(cts)) if cts else "-")


def _has_security(sec) -> bool:
    if sec is None:
        return False
    if isinstance(sec, list):
        return any(bool(item) for item in sec)
    if isinstance(sec, dict):
        return any(bool(v) for v in sec.values())
    return False


def render(rows) -> str:
    if not rows:
        raise SystemExit("[parse_endpoints] error: no endpoints extracted")
    return "\n".join("\t".join(row) for row in rows) + "\n"


# ---------------------------------------------------------------------------
# Self-test
# ---------------------------------------------------------------------------

_FIXTURE_DIR = Path(__file__).resolve().parent / "fixtures"


def _selftest() -> int:
    fx = _FIXTURE_DIR / "openapi-mini.yaml"
    if not fx.exists():
        print(f"[parse_endpoints][err] missing fixture: {fx}", file=sys.stderr)
        return 2
    doc = _load_yaml(fx.read_text(encoding="utf-8"))
    rows = extract_endpoints(doc)
    if len(rows) != 4:
        print(
            f"[parse_endpoints][err] expected 4 rows from openapi-mini.yaml, got {len(rows)}",
            file=sys.stderr,
        )
        for r in rows:
            print("  ", r, file=sys.stderr)
        return 1
    rendered = render(rows)
    lines = rendered.splitlines()
    if lines[0] != "GET\t/.well-known/openid-configuration\t200\tapplication/json\tOpenID Connect\tno":
        print(f"[parse_endpoints][err] first row mismatch: {lines[0]!r}", file=sys.stderr)
        return 1
    expect_token = "POST\t/oauth2/token\t200,400\tapplication/json\tOAuth2\tyes"
    if expect_token not in lines:
        print(f"[parse_endpoints][err] missing expected token row: {expect_token!r}", file=sys.stderr)
        for ln in lines:
            print("  ", ln, file=sys.stderr)
        return 1
    if not any(ln.startswith("GET\t/api/admin/clients\t") and "\tyes" in ln for ln in lines):
        print("[parse_endpoints][err] admin GET row missing or not marked secured", file=sys.stderr)
        return 1
    if not any(ln.startswith("POST\t/api/admin/clients\t201\t") for ln in lines):
        print("[parse_endpoints][err] admin POST row missing or wrong status", file=sys.stderr)
        return 1
    print(f"[parse_endpoints] selftest OK ({len(rows)} endpoint rows).")
    return 0


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------


def _main() -> int:
    p = argparse.ArgumentParser(
        description="Extract endpoint signatures from an OpenAPI document"
    )
    p.add_argument("--openapi", help="path to OpenAPI 3.x YAML/JSON file")
    p.add_argument("--out", help="path to write parsed baseline")
    p.add_argument(
        "--print",
        dest="print_only",
        action="store_true",
        help="print parsed baseline to stdout instead of writing --out",
    )
    p.add_argument("--selftest", action="store_true")
    args = p.parse_args()

    if args.selftest:
        return _selftest()

    if not args.openapi:
        p.error("--openapi is required (or use --selftest)")

    text = Path(args.openapi).read_text(encoding="utf-8")
    doc = _load_yaml(text)
    if not isinstance(doc, dict):
        print("[parse_endpoints][err] OpenAPI root is not a mapping", file=sys.stderr)
        return 1
    rows = extract_endpoints(doc)
    rendered = render(rows)

    if args.print_only or not args.out:
        sys.stdout.write(rendered)
        return 0

    out_path = Path(args.out)
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(rendered, encoding="utf-8", newline="\n")
    print(f"[parse_endpoints] wrote {len(rows)} endpoint rows to {out_path}")
    return 0


if __name__ == "__main__":
    sys.exit(_main())
