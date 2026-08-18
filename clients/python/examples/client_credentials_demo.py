"""End-to-end demo: client_credentials against a running AuthForge server.

Usage:
    python examples/client_credentials_demo.py [base_url] [client_id] [client_secret]

Defaults match the development seed (apps/server/seed/dev_backend_client.sql):
backend-svc / test-secret on http://127.0.0.1:5555. Start the full stack first
(see the repo README); this script is the Python analogue of benchmark
scenario S2 (client_credentials token flow) plus introspection.

Exit code 0 = all steps passed; non-zero = the first failure.
"""

from __future__ import annotations

import os
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "src"))

from authforge import basic_auth_client, fetch_client_credentials_token  # noqa: E402
from authforge.generated.api.o_auth_2 import post_oauth2_introspect  # noqa: E402
from authforge.generated.models.post_oauth_2_introspect_body import (  # noqa: E402
    PostOauth2IntrospectBody,
)


def main() -> int:
    base_url = sys.argv[1] if len(sys.argv) > 1 else os.environ.get("AUTHFORGE_BASE_URL", "http://127.0.0.1:5555")
    client_id = sys.argv[2] if len(sys.argv) > 2 else os.environ.get("AUTHFORGE_CLIENT_ID", "backend-svc")
    client_secret = sys.argv[3] if len(sys.argv) > 3 else os.environ.get("AUTHFORGE_CLIENT_SECRET", "test-secret")

    print(f"[demo] fetching client_credentials token from {base_url} as {client_id}")
    access_token, expires_in = fetch_client_credentials_token(
        base_url, client_id, client_secret, ["tokens:read"]
    )
    print(f"[demo] got access_token ({len(access_token)} chars), expires_in={expires_in}s")

    client = basic_auth_client(base_url, client_id, client_secret)
    result = post_oauth2_introspect.sync(
        client=client, body=PostOauth2IntrospectBody(token=access_token)
    )
    if not result.active:
        print("[demo] FAIL: freshly obtained token introspects as inactive")
        return 1
    print(f"[demo] introspect: active=True client_id={result.client_id} scope={result.scope}")
    print("[demo] OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
