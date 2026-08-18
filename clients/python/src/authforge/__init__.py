"""AuthForge Python client SDK.

Distribution name ``authforge-oauth2`` (the shorter ``authforge`` name on
PyPI belongs to an unrelated project); import name ``authforge``.

Public API:

- :func:`m2m_client` / :func:`async_m2m_client` -- ready-to-use generated
  clients with automatic client_credentials token management.
- :func:`basic_auth_client` -- HTTP-Basic client for introspect/revoke.
- :func:`fetch_client_credentials_token` -- one-shot token fetch.
- :class:`AuthorizationCodeFlow` / :class:`PkcePair` -- authorization-code
  grant helpers (URL building, PKCE S256, code exchange, refresh).
- :mod:`authforge.generated` -- the full typed API surface generated from
  apps/server/openapi.yaml (see its own README for endpoint modules).
"""

from .m2m import (
    AsyncClientCredentialsAuth,
    AuthForgeAuthError,
    ClientCredentialsAuth,
    async_m2m_client,
    basic_auth_client,
    fetch_client_credentials_token,
    m2m_client,
)
from .oauth import AuthorizationCodeFlow, PkcePair, create_pkce_challenge

try:  # pragma: no cover - packaging-time fallback only
    from importlib.metadata import PackageNotFoundError, version

    __version__ = version("authforge-oauth2")
except PackageNotFoundError:  # pragma: no cover
    __version__ = "0.0.0+unknown"

__all__ = [
    "__version__",
    "AsyncClientCredentialsAuth",
    "AuthForgeAuthError",
    "AuthorizationCodeFlow",
    "ClientCredentialsAuth",
    "PkcePair",
    "async_m2m_client",
    "basic_auth_client",
    "create_pkce_challenge",
    "fetch_client_credentials_token",
    "m2m_client",
]
