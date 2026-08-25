#!/usr/bin/env python3
"""parse-wrk.py — convert wrk stdout into a structured benchmark result JSON.

Reads wrk's native text output from stdin, parses the numbers that matter for
the benchmark facility (QPS, latency percentiles, error counts), and emits a
single JSON object on stdout. Environment metadata (scenario, concurrency,
hardware, git sha, ...) is supplied via CLI flags / env vars by run-scenario.sh;
this script only owns the wrk-text -> numbers translation.

Usage:
    wrk ... | python3 parse-wrk.py \
        --scenario s2-client-credentials \
        --concurrency 64 \
        --threads 4 \
        --duration 30 \
        --driver-cpu 42.5 \
        [ --git-sha abc123 ... ]

The emitted JSON conforms to the schema documented in
benchmarks/fulla/lib/result-schema.md. This is the parsing layer that M4's
gen-summary report generator will aggregate across runs.
"""
from __future__ import annotations

import argparse
import json
import os
import re
import sys
from datetime import datetime, timezone


# wrk prints latencies with a unit suffix (s / ms / us / m for minutes only on
# the max line, never on percentiles). Normalise everything to microseconds.
_UNIT_TO_US = {"us": 1, "µs": 1, "ms": 1_000, "s": 1_000_000, "m": 60_000_000}
_NUMBER = r"([\d.,]+)"


def _to_us(value: str, unit: str) -> float:
    """Normalise a wrk latency value+unit to microseconds."""
    return float(value.replace(",", "")) * _UNIT_TO_US.get(unit, 1)


def _parse_latency_percentiles(text: str) -> dict:
    """Extract the Latency Distribution percentile block (P50/75/90/95/99/max).

    wrk emits one of:
        50%    1.10ms
        Max    12.30ms
    Returns a dict of percentile->us (only the lines present). 'max' is keyed
    separately so callers can tell wrk's summary max from the percentile block.
    """
    out: dict[str, float] = {}
    pct_re = re.compile(
        r"^\s*(50%|75%|90%|95%|99%|Max)\s+"
        + _NUMBER
        + r"\s*(us|µs|ms|s|m)\s*$",
        re.MULTILINE,
    )
    for m in pct_re.finditer(text):
        key = m.group(1).replace("%", "")
        key = "max" if key.lower() == "max" else f"p{key}"
        out[key] = _to_us(m.group(2), m.group(3))
    return out


def _parse_qps(text: str) -> float | None:
    m = re.search(r"Requests/sec:\s*" + _NUMBER, text)
    return float(m.group(1).replace(",", "")) if m else None


def _parse_total_requests(text: str) -> int | None:
    m = re.search(r"(\d[\d,]*)\s+requests\s+in\s+([\d.]+)s", text)
    return int(m.group(1).replace(",", "")) if m else None


def _parse_socket_errors(text: str) -> dict:
    out = {}
    m = re.search(r"Socket errors:\s*connect\s+(\d+),\s*read\s+(\d+),\s*write\s+(\d+),\s*timeout\s+(\d+)", text)
    if m:
        out = {
            "connect": int(m.group(1)),
            "read": int(m.group(2)),
            "write": int(m.group(3)),
            "timeout": int(m.group(4)),
        }
    return out


def _parse_non_2xx(text: str) -> int | None:
    m = re.search(r"Non-2xx or 3xx responses:\s*(\d+)", text)
    return int(m.group(1)) if m else None


def parse_wrk(text: str) -> dict:
    """Parse wrk text output into a flat metrics dict (no env metadata)."""
    qps = _parse_qps(text)
    total = _parse_total_requests(text)
    latencies = _parse_latency_percentiles(text)
    socket_errors = _parse_socket_errors(text)
    non_2xx = _parse_non_2xx(text)

    error_count = (non_2xx or 0) + sum(socket_errors.values())
    error_rate = (error_count / total) if (total and total > 0) else 0.0

    return {
        "qps": qps,
        "total_requests": total,
        "latency_us": latencies,
        "socket_errors": socket_errors,
        "non_2xx_responses": non_2xx,
        "error_count": error_count,
        "error_rate": error_rate,
    }


def _collect_env(args: argparse.Namespace) -> dict:
    """Assemble the env-metadata block from flags + BENCH_* env vars."""
    return {
        "date": args.date or datetime.now(timezone.utc).isoformat(timespec="seconds"),
        "git_sha": args.git_sha or os.environ.get("BENCH_GIT_SHA", ""),
        "git_branch": args.git_branch or os.environ.get("BENCH_GIT_BRANCH", ""),
        "product": args.product or os.environ.get("BENCH_PRODUCT", "fulla"),
        "product_version": args.product_version or os.environ.get("BENCH_PRODUCT_VERSION", ""),
        "target_image": args.target_image or os.environ.get("BENCH_TARGET_IMAGE", ""),
        "target_config": args.target_config or os.environ.get("BENCH_TARGET_CONFIG", "config.json"),
        "target_spec": args.target_spec or os.environ.get("BENCH_TARGET_SPEC", ""),
        "driver_spec": args.driver_spec or os.environ.get("BENCH_DRIVER_SPEC", ""),
        "network": args.network or os.environ.get("BENCH_NETWORK", "localhost-cross-container"),
        "wrk_version": args.wrk_version or os.environ.get("BENCH_WRK_VERSION", ""),
    }


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--scenario", required=True, help="scenario name (e.g. s2-client-credentials)")
    p.add_argument("--concurrency", type=int, required=True, help="wrk -c (connections)")
    p.add_argument("--threads", type=int, required=True, help="wrk -t (threads)")
    p.add_argument("--duration", type=int, required=True, help="measured run duration in seconds")
    p.add_argument("--driver-cpu", type=float, help="wrk process peak CPU%% during the run")
    p.add_argument("--git-sha")
    p.add_argument("--git-branch")
    p.add_argument("--product", help="target product identity (fulla | keycloak | ory | zitadel); default fulla")
    p.add_argument("--product-version", help="target product version/image tag recorded in the env block")
    p.add_argument("--target-image")
    p.add_argument("--target-config")
    p.add_argument("--target-spec")
    p.add_argument("--driver-spec")
    p.add_argument("--network")
    p.add_argument("--date")
    p.add_argument("--wrk-version")
    args = p.parse_args()

    wrk_text = sys.stdin.read()
    if not wrk_text.strip():
        print("error: no wrk output on stdin", file=sys.stderr)
        return 2

    metrics = parse_wrk(wrk_text)

    driver_cpu = args.driver_cpu
    driver_limited = bool(driver_cpu is not None and driver_cpu >= 80.0)

    result = {
        "schema_version": 1,
        "scenario": args.scenario,
        "concurrency": args.concurrency,
        "threads": args.threads,
        "duration_s": args.duration,
        "qps": metrics["qps"],
        "latency_us": metrics["latency_us"],
        "total_requests": metrics["total_requests"],
        "error_rate": metrics["error_rate"],
        "error_count": metrics["error_count"],
        "socket_errors": metrics["socket_errors"],
        "non_2xx_responses": metrics["non_2xx_responses"],
        "driver": {
            "cpu_pct": driver_cpu,
            "limited": driver_limited,
        },
        "env": _collect_env(args),
    }
    json.dump(result, sys.stdout, indent=2)
    sys.stdout.write("\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
