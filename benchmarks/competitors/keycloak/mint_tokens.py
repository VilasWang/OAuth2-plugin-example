#!/usr/bin/env python3
"""mint_tokens.py — batch-issue Keycloak tokens for benchmark pools (design D5).

Competitor token pools MUST be issued by the product itself (no SQL seeding:
signed JWTs). Mints tokens over Keycloak's token endpoint in parallel and
prints them to stdout, one per line; progress goes to stderr. The caller
redirects stdout into the pool file:

  python3 mint_tokens.py --url http://localhost:8080 --realm bench \
      --client-id bench-svc --client-secret bench-secret \
      --grant client_credentials --count 2000 \
      > lib/generated/cc_tokens.txt

  python3 mint_tokens.py --url ... --grant password --username bench-user \
      --password bench-pass --scope openid --extract refresh_token \
      --count 30000 > lib/generated/refresh_tokens.txt

Grants:
  * client_credentials — service-account access tokens (S3 introspect pool).
  * password (ROPC)    — user access + refresh tokens (S6 / S5 pools). Direct
    access grants are Keycloak's documented headless user-token path
    (https://www.keycloak.org/docs/latest/securing_apps/#direct-access-grants).
"""
from __future__ import annotations

import argparse
import sys
import threading
import time
from concurrent.futures import ThreadPoolExecutor

import requests


def mint_one(session: requests.Session, args: argparse.Namespace) -> str:
    data = {"grant_type": args.grant, "client_id": args.client_id}
    if args.client_secret:
        data["client_secret"] = args.client_secret
    if args.grant == "password":
        data["username"] = args.username
        data["password"] = args.password
        if args.scope:
            data["scope"] = args.scope
    r = session.post(args.url + "/realms/" + args.realm + "/protocol/openid-connect/token",
                     data=data, timeout=15)
    r.raise_for_status()
    tok = r.json().get(args.extract)
    if not tok:
        raise ValueError(f"response missing {args.extract}: {r.text[:200]}")
    return tok


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--url", required=True, help="Keycloak base URL, e.g. http://localhost:8080")
    p.add_argument("--realm", required=True)
    p.add_argument("--client-id", required=True)
    p.add_argument("--client-secret", default="")
    p.add_argument("--grant", choices=["client_credentials", "password"], required=True)
    p.add_argument("--username", default="")
    p.add_argument("--password", default="")
    p.add_argument("--scope", default="", help="e.g. openid (ROPC user tokens)")
    p.add_argument("--extract", default="access_token",
                   choices=["access_token", "refresh_token"])
    p.add_argument("--count", type=int, required=True)
    p.add_argument("--parallel", type=int, default=8)
    p.add_argument("--retries", type=int, default=3)
    args = p.parse_args()

    lock = threading.Lock()
    done = [0]
    failed = [0]

    def work(i: int) -> None:
        session = requests.Session()
        tok = None
        for attempt in range(args.retries):
            try:
                tok = mint_one(session, args)
                break
            except Exception as e:  # noqa: BLE001 — retry any transient error
                if attempt == args.retries - 1:
                    with lock:
                        failed[0] += 1
                        if failed[0] <= 3:
                            print(f"  [mint] #{i} failed: {e}", file=sys.stderr)
                else:
                    time.sleep(0.1 * (attempt + 1))
        if tok:
            with lock:
                sys.stdout.write(tok + "\n")
                done[0] += 1
                if done[0] % 2000 == 0:
                    print(f"  [mint] {done[0]}/{args.count}", file=sys.stderr)

    t0 = time.time()
    with ThreadPoolExecutor(max_workers=args.parallel) as pool:
        list(pool.map(work, range(args.count)))
    sys.stdout.flush()

    rate = done[0] / max(time.time() - t0, 0.001)
    print(f"[mint] {done[0]}/{args.count} ok ({failed[0]} failed) "
          f"in {time.time() - t0:.1f}s ({rate:.0f}/s)", file=sys.stderr)
    # Let partial pools through (the runner warns on short pools), but fail
    # hard when more than 1% failed — that means the target is misconfigured.
    return 1 if failed[0] > args.count // 100 else 0


if __name__ == "__main__":
    raise SystemExit(main())
