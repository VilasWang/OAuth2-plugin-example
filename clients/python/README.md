# AuthForge Python Client

Typed Python SDK for [AuthForge](https://github.com/lucaswang420/authforge) — the embeddable
C++ OAuth2/OIDC authorization server.

- **Distribution name:** `authforge-oauth2` (the shorter `authforge` name on PyPI belongs to an
  unrelated project)
- **Import name:** `authforge`
- **API surface:** generated from the server's single-source OpenAPI spec
  (`apps/server/openapi.yaml`) with [openapi-python-client](https://github.com/openapi-generators/openapi-python-client)
  0.29.0 — 78 operations, fully typed (`py.typed`), `attrs`-based models over `httpx`
- **Auth layer:** handwritten (token lifecycle is too sensitive to template — see the
  [design doc](../../docs/productization-evolution/in-progress/client-sdk-facility-design.md))

```bash
pip install authforge-oauth2
```

> Publishing to PyPI starts with the first tagged release that includes this package. Until
> then install from a checkout: `pip install clients/python`.

## Quickstart: machine-to-machine (client_credentials)

```python
from authforge import m2m_client
from authforge.generated.api.o_auth_2 import post_oauth2_introspect
from authforge.generated.models.post_oauth_2_introspect_body import PostOauth2IntrospectBody

client = m2m_client(
    "http://localhost:5555",
    client_id="backend-svc",
    client_secret="…",
    scopes=["tokens:read"],
)

# Every request carries a valid Bearer token: fetched lazily, cached,
# refreshed 30 s before expiry, force-refreshed once on a 401.
result = post_oauth2_introspect.sync(
    client=client, body=PostOauth2IntrospectBody(token=some_access_token)
)
print(result.active)

# When done: closes BOTH the API client and the auth layer's token pool
# (closing the generated client alone leaks the token connection pool).
from authforge import close_m2m_client
close_m2m_client(client)
```

Async is symmetric:

```python
from authforge import async_m2m_client

client = async_m2m_client("http://localhost:5555", "backend-svc", "…", scopes=["tokens:read"])
result = await post_oauth2_introspect.asyncio(client=client, body=body)
```

One-shot token fetch (scripts, benchmarks):

```python
from authforge import fetch_client_credentials_token

token, expires_in = fetch_client_credentials_token(
    "http://localhost:5555", "backend-svc", "…", ["tokens:read"]
)
```

## Introspect / revoke (client Basic authentication)

`/oauth2/introspect` and `/oauth2/revoke` authenticate the *calling client*, not a user bearer
token (RFC 7662 §2.1). Use a client whose every request carries HTTP Basic — confidential
clients must use Basic; the server rejects credentials in the body (F-017):

```python
from authforge import basic_auth_client

client = basic_auth_client("http://localhost:5555", "backend-svc", "…")
result = post_oauth2_introspect.sync(client=client, body=PostOauth2IntrospectBody(token=tok))
```

## Authorization-code flow (web apps, with PKCE)

```python
from authforge import AuthorizationCodeFlow, PkcePair

flow = AuthorizationCodeFlow(
    "http://localhost:5555", "my-client", "my-secret",
    redirect_uri="https://my.app/callback", scopes=["openid", "profile"],
)
pkce = PkcePair.generate()
authorize_url = flow.build_authorize_url(state=session_csrf, pkce=pkce)
# → send the user's browser to authorize_url; AuthForge redirects back with ?code=…&state=…
# → VERIFY state, then:
tokens = flow.exchange_code(code, pkce.verifier)
# … later; AuthForge rotates refresh tokens on every use (V008):
tokens = flow.refresh(tokens.refresh_token)
```

## Generated API modules

Everything under `authforge.generated` is the typed surface of the whole server API:

```python
from authforge.generated.api.open_id_connect import get_well_known_openid_configuration
from authforge.generated.api.o_auth_2 import get_oauth2_userinfo, post_oauth2_token
from authforge.generated.models.token_request import TokenRequest
```

Each endpoint module offers `sync`, `sync_detailed`, `asyncio`, `asyncio_detailed`. The
`*_detailed` variants return status code + raw response alongside the parsed model.

## Development

```bash
cd clients/python
pip install -e ".[dev]"
pytest                       # unit tests (in-process MockTransport, no server needed)

# integration tests (needs a running full stack, see tests/integration/)
AUTHFORGE_BASE_URL=http://127.0.0.1:5555 pytest tests/integration
```

Regenerating the committed `src/authforge/generated/` tree after an `openapi.yaml` change:

```bash
pip install openapi-python-client==0.29.0
python tools/clients/regen_clients.py            # from the repo root
```

CI (`.github/workflows/clients-sdk.yml`) re-generates and diffs on every PR touching
`clients/**` or the spec — committed generated code can never go stale.

## Versioning

The package version is locked to the server's `cmake/Version.cmake` (enforced by
`tools/clients/regen_clients.py --version-only` at release time). Breaking HTTP API changes
require a major bump on both sides (guarded by the `openapi-governance` oasdiff workflow).

## Local network note

If `go`/module proxies are unreachable from your network, the Go generator download mentioned
in the regen docs needs a GOPROXY mirror (e.g. `GOPROXY=https://goproxy.cn,direct`). This only
affects regenerating `clients/go` — installing and using this Python package is unaffected.
