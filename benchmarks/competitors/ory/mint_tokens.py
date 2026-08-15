#!/usr/bin/env python3
"""mint_tokens.py — batch-issue Ory Hydra tokens for benchmark pools (design D5).

Competitor token pools MUST be issued by the product itself. Hydra has no
built-in user store, so user-context tokens are obtained through Hydra's
official headless mock pattern: drive /oauth2/auth, accept the login +
consent challenges via the ADMIN API, exchange the code
(docs: https://www.ory.sh/docs/hydra/guides/login-login-consent-flow).

Modes (stdout = one token per line, progress on stderr):
  cc-token    client_credentials access tokens          (S3 introspect pool)
  user-at     authorization_code user access tokens     (S6 userinfo pool)
  user-rt     authorization_code user refresh tokens    (S5 one-shot pool)

Usage:
  python3 mint_tokens.py --public http://127.0.0.1:4444 \
      --admin http://127.0.0.1:4445 --client-id bench-svc --client-secret S \
      --mode cc-token --count 2000 > lib/generated/cc_tokens.txt
  python3 mint_tokens.py ... --client-id bench-web --client-secret S --mode user-rt \
      --count 20000 --redirect-uri http://127.0.0.1:4444/unused > lib/generated/refresh_tokens.txt
"""
from __future__ import annotations

import argparse
import sys
import threading
import time
import urllib.parse
from concurrent.futures import ThreadPoolExecutor

import requests


def oauth2_auth_flow(public: str, admin: str, client_id: str, client_secret: str,
                     redirect_uri: str) -> dict:
    """Full headless accept dance; returns the token-endpoint JSON response."""
    s = requests.Session()
    # 1. start the flow — do NOT follow the redirect (login URL is a placeholder)
    r1 = s.get(public + "/oauth2/auth", params={
        "client_id": client_id,
        "response_type": "code",
        "scope": "openid offline_access",
        "state": "bench",
        "redirect_uri": redirect_uri,
        "nonce": str(time.time_ns()),
    }, allow_redirects=False, timeout=15)
    login_challenge = urllib.parse.parse_qs(
        urllib.parse.urlparse(r1.headers.get("Location", "")).query).get("login_challenge", [None])[0]
    if not login_challenge:
        raise ValueError(f"no login_challenge in redirect: {r1.status_code} {r1.headers.get('Location')}")
    # 2. accept login (admin API)
    r2 = s.post(admin + "/admin/oauth2/auth/requests/login/accept",
                params={"login_challenge": login_challenge},
                json={"subject": "bench-user", "remember": False}, timeout=15)
    r2.raise_for_status()
    # 3. continue — consent challenge appears in the next redirect
    r3 = s.get(r2.json()["redirect_to"], allow_redirects=False, timeout=15)
    consent_challenge = urllib.parse.parse_qs(
        urllib.parse.urlparse(r3.headers.get("Location", "")).query).get("consent_challenge", [None])[0]
    if not consent_challenge:
        raise ValueError(f"no consent_challenge in redirect: {r3.status_code} {r3.headers.get('Location')}")
    # 4. accept consent (admin API)
    r4 = s.post(admin + "/admin/oauth2/auth/requests/consent/accept",
                params={"consent_challenge": consent_challenge},
                json={"grant_scope": ["openid", "offline_access"], "remember": False},
                timeout=15)
    r4.raise_for_status()
    # 5. continue — the authorization code comes back on the redirect_uri
    r5 = s.get(r4.json()["redirect_to"], allow_redirects=False, timeout=15)
    q = urllib.parse.parse_qs(urllib.parse.urlparse(r5.headers.get("Location", "")).query)
    code = q.get("code", [None])[0]
    if not code:
        raise ValueError(f"no code in redirect: {r5.status_code} {r5.headers.get('Location')}")
    # 6. exchange
    r6 = s.post(public + "/oauth2/token",
                auth=(client_id, client_secret),
                data={"grant_type": "authorization_code", "code": code,
                      "redirect_uri": redirect_uri},
                timeout=15)
    r6.raise_for_status()
    return r6.json()


def mint_one(args: argparse.Namespace, session: requests.Session) -> str:
    if args.mode == "cc-token":
        r = session.post(args.public + "/oauth2/token",
                         auth=(args.client_id, args.client_secret),
                         data={"grant_type": "client_credentials"}, timeout=15)
        r.raise_for_status()
        tok = r.json()["access_token"]
    else:
        resp = oauth2_auth_flow(args.public, args.admin, args.client_id,
                                args.client_secret, args.redirect_uri)
        tok = resp["refresh_token"] if args.mode == "user-rt" else resp["access_token"]
    return tok


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--public", required=True, help="Hydra public URL, e.g. http://127.0.0.1:4444")
    p.add_argument("--admin", required=True, help="Hydra admin URL, e.g. http://127.0.0.1:4445")
    p.add_argument("--client-id", required=True)
    p.add_argument("--client-secret", required=True)
    p.add_argument("--mode", choices=["cc-token", "user-at", "user-rt"], required=True)
    p.add_argument("--redirect-uri", default="http://127.0.0.1:4444/unused")
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
                tok = mint_one(args, session)
                break
            except Exception as e:  # noqa: BLE001 — retry transient errors
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
    return 1 if failed[0] > args.count // 100 else 0


if __name__ == "__main__":
    raise SystemExit(main())
