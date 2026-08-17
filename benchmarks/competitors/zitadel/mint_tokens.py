#!/usr/bin/env python3
"""mint_tokens.py — Zitadel token/assertion minting for benchmark pools (D5).

Zitadel's official machine-to-machine path is Service User + JWT profile:
RFC 7523 jwt-bearer GRANT at the token endpoint (assertion = signed JWT),
https://zitadel.com/docs/guides/integrate/service-accounts/private-key-jwt
The machine key JSON (bootstrap-generated, printed to stdout by
FirstInstance setup — extracted from docker logs by setup.sh) holds an RSA
key; we sign RS256 assertions with pyjwt. iss = sub = the machine userId,
aud = issuer, kid = keyId.

Modes (stdout = one item per line, progress on stderr):
  assertion   signed assertion JWTs (S2 pool; usually ONE reusable
              within exp is enough — pass --count 1 and test reuse first)
  cc-token    access tokens via the jwt-bearer grant (S3 introspect pool /
              S6 userinfo pool)

Usage:
  python3 mint_tokens.py --issuer http://localhost:8080 \
      --key lib/generated/machinekey.json --mode cc-token --count 2000 \
      --scope "openid urn:zitadel:iam:org:project:id:<projid>:aud" \
      > lib/generated/cc_tokens.txt
"""
from __future__ import annotations

import argparse
import json
import sys
import threading
import time
from concurrent.futures import ThreadPoolExecutor

import jwt
import requests


def load_key(path: str) -> dict:
    with open(path, encoding="utf-8") as f:
        return json.load(f)


def make_assertion(key: dict, issuer: str, login_name: str, ttl_s: int) -> str:
    now = int(time.time())
    payload = {
        "iss": login_name,
        "sub": login_name,
        "aud": issuer,
        "iat": now,
        "exp": now + ttl_s,
        # Zitadel rejects assertions older than this leeway window
    }
    headers = {"kid": key["id"]}
    return jwt.encode(payload, key["key"], algorithm="RS256", headers=headers)


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--issuer", required=True, help="Zitadel external issuer, e.g. http://localhost:8080")
    p.add_argument("--key", required=True, help="machine key JSON path")
    p.add_argument("--login-name", default="", help="machine login name; default derived from key JSON")
    p.add_argument("--mode", choices=["assertion", "cc-token"], required=True)
    p.add_argument("--scope", default="openid profile", help="token scope for cc-token mode")
    p.add_argument("--count", type=int, required=True)
    p.add_argument("--parallel", type=int, default=8)
    p.add_argument("--assertion-ttl", type=int, default=3600,
                   help="assertion lifetime (s); must cover the whole run window")
    args = p.parse_args()

    key = load_key(args.key)
    # key files from the Management API use "id"; the FirstInstance stdout
    # JSON (extracted from docker logs by setup.sh) uses "keyId" — normalize
    key.setdefault("id", key.get("keyId", ""))
    login = args.login_name or key.get("loginName") or key.get("userId")

    lock = threading.Lock()
    done = [0]
    failed = [0]

    def work(i: int) -> None:
        out = None
        if args.mode == "assertion":
            out = make_assertion(key, args.issuer, login, args.assertion_ttl)
        else:
            last_err: Exception | None = None
            for attempt in range(5):  # transient 404/500 blips under parallel mint load
                try:
                    assertion = make_assertion(key, args.issuer, login, 600)
                    r = requests.post(
                        args.issuer + "/oauth/v2/token",
                        data={
                            # RFC 7523 §4.1 jwt-bearer GRANT — Zitadel's official
                            # M2M path for Service Users. The token endpoint does
                            # NOT accept client_credentials with a JWT assertion
                            # (v2.71 VerifyClient routes that grant to the
                            # Basic-secret machine path instead).
                            "grant_type": "urn:ietf:params:oauth:grant-type:jwt-bearer",
                            "scope": args.scope,
                            "assertion": assertion,
                        },
                        timeout=15,
                    )
                    r.raise_for_status()
                    out = r.json()["access_token"]
                    last_err = None
                    break
                except Exception as e:  # noqa: BLE001
                    last_err = e
                    time.sleep(1.0 * (attempt + 1))
            if out is None:
                with lock:
                    failed[0] += 1
                    if failed[0] <= 3:
                        print(f"  [mint] #{i} failed: {last_err}", file=sys.stderr)
                return
        with lock:
            sys.stdout.write(out + "\n")
            done[0] += 1
            if done[0] % 1000 == 0:
                print(f"  [mint] {done[0]}/{args.count}", file=sys.stderr)

    t0 = time.time()
    if args.mode == "assertion" and args.count == 1:
        # single reusable assertion — no threads needed
        sys.stdout.write(make_assertion(key, args.issuer, login, args.assertion_ttl) + "\n")
        done[0] = 1
    else:
        with ThreadPoolExecutor(max_workers=args.parallel) as pool:
            list(pool.map(work, range(args.count)))
    sys.stdout.flush()

    print(f"[mint] {done[0]}/{args.count} ok ({failed[0]} failed) "
          f"in {time.time() - t0:.1f}s", file=sys.stderr)
    return 1 if done[0] < args.count else 0


if __name__ == "__main__":
    raise SystemExit(main())
