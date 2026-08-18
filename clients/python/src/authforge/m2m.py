"""Machine-to-machine (client_credentials) authentication for AuthForge.

This module is the blessed entry point for the most common SDK usage: a
confidential client talking to AuthForge's token endpoint with the
``client_credentials`` grant (RFC 6749 §4.4) and then calling bearer-protected
APIs with automatic token acquisition, proactive refresh, and one 401-triggered
retry.

Design notes (docs/productization-evolution/in-progress/client-sdk-facility-design.md
section 11.4):

- Token lifecycle is HANDWRITTEN, never generated (design D5): refresh
  rotation, lockout and family revocation are too sensitive to template.
- Confidential client credentials always travel as HTTP Basic
  (``token_endpoint_auth_method=client_secret_basic``; the server rejects the
  body form for such clients -- F-017).
- The generated ``AuthenticatedClient`` is injected with an ``httpx.Client``
  carrying our ``httpx.Auth``; that injection bypasses the generated client's
  static bearer-header construction (it only runs when the generated client
  lazily builds its OWN httpx client).

Sync and async are separate classes on purpose: httpx drives ``auth_flow``
matching the client flavour, and the async flavour must not block the event
loop on token refresh I/O.
"""

from __future__ import annotations

import asyncio
import base64
import threading
import time
from typing import Iterable, Optional, Sequence, Tuple

import httpx

# 0.29.0 generates a single Client/AuthenticatedClient pair that serves both
# sync and async (the flavour is chosen by which httpx client is injected).
from .generated.client import AuthenticatedClient

#: Refresh this many seconds before the advertised expiry (clock skew margin).
DEFAULT_CLOCK_SKEW = 30.0

__all__ = [
    "AuthForgeAuthError",
    "ClientCredentialsAuth",
    "AsyncClientCredentialsAuth",
    "m2m_client",
    "async_m2m_client",
    "basic_auth_client",
    "fetch_client_credentials_token",
]


class AuthForgeAuthError(RuntimeError):
    """The token endpoint rejected the client (RFC 6749 section 5.2 body)."""

    def __init__(
        self,
        error: str,
        error_description: Optional[str] = None,
        status_code: Optional[int] = None,
    ) -> None:
        self.error = error
        self.error_description = error_description
        self.status_code = status_code
        message = f"token endpoint rejected client: {error}"
        if error_description:
            message += f" ({error_description})"
        if status_code is not None:
            message += f" [HTTP {status_code}]"
        super().__init__(message)


def _token_request_data(scopes: Sequence[str]) -> dict:
    data = {"grant_type": "client_credentials"}
    if scopes:
        data["scope"] = " ".join(scopes)
    return data


def _parse_token_response(resp: httpx.Response) -> Tuple[str, float]:
    if resp.status_code == 200:
        body = resp.json()
        token = body.get("access_token")
        if not token:
            raise AuthForgeAuthError(
                "invalid_response", "200 without access_token", resp.status_code
            )
        return token, float(body.get("expires_in", 3600))
    try:
        body = resp.json()
    except ValueError:
        body = {}
    raise AuthForgeAuthError(
        body.get("error", "invalid_response"),
        body.get("error_description"),
        resp.status_code,
    )


def _normalize_base_url(base_url: str) -> str:
    return base_url.rstrip("/")


def _basic_header(client_id: str, client_secret: str) -> str:
    raw = f"{client_id}:{client_secret}".encode()
    return "Basic " + base64.b64encode(raw).decode()


def fetch_client_credentials_token(
    base_url: str,
    client_id: str,
    client_secret: str,
    scopes: Iterable[str] = (),
    *,
    timeout: float = 10.0,
    verify_ssl: bool = True,
    transport: Optional[httpx.BaseTransport] = None,
) -> Tuple[str, float]:
    """One-shot client_credentials token fetch (no caching).

    Returns ``(access_token, expires_in_seconds)``. Useful for scripts and
    benchmarks; long-lived callers should prefer :func:`m2m_client`.
    """
    with httpx.Client(transport=transport, timeout=timeout, verify=verify_ssl) as http:
        resp = http.post(
            _normalize_base_url(base_url) + "/oauth2/token",
            data=_token_request_data(tuple(scopes)),
            auth=(client_id, client_secret),
        )
        return _parse_token_response(resp)


class ClientCredentialsAuth(httpx.Auth):
    """Sync httpx auth: Bearer injection + proactive/401-triggered refresh.

    Thread safe: concurrent requests share one cached token; refresh is
    serialized under a lock (double-checked).
    """

    def __init__(
        self,
        token_url: str,
        client_id: str,
        client_secret: str,
        scopes: Sequence[str] = (),
        *,
        clock_skew: float = DEFAULT_CLOCK_SKEW,
        timeout: float = 10.0,
        verify_ssl: bool = True,
        transport: Optional[httpx.BaseTransport] = None,
    ) -> None:
        self._token_url = token_url
        self._client_id = client_id
        self._client_secret = client_secret
        self._scopes = tuple(scopes)
        self._clock_skew = clock_skew
        self._lock = threading.Lock()
        self._access_token: Optional[str] = None
        self._expires_at = 0.0
        self._http = httpx.Client(transport=transport, timeout=timeout, verify=verify_ssl)

    def close(self) -> None:
        self._http.close()

    def __enter__(self) -> "ClientCredentialsAuth":
        return self

    def __exit__(self, *exc: object) -> None:
        self.close()

    def auth_flow(self, request: httpx.Request):  # type: ignore[override]
        request.headers["Authorization"] = "Bearer " + self._get_token(force=False)
        response = yield request
        if response.status_code == 401:
            # Token revoked/expired server-side ahead of our schedule: force
            # one refresh and retry once. A second 401 is passed through.
            request.headers["Authorization"] = "Bearer " + self._get_token(force=True)
            yield request

    def _get_token(self, *, force: bool) -> str:
        with self._lock:
            if (
                not force
                and self._access_token is not None
                and self._expires_at - self._clock_skew > time.time()
            ):
                return self._access_token
            token, lifetime = self._request_token()
            self._access_token = token
            self._expires_at = time.time() + lifetime
            return token

    def _request_token(self) -> Tuple[str, float]:
        resp = self._http.post(
            self._token_url,
            data=_token_request_data(self._scopes),
            auth=(self._client_id, self._client_secret),
        )
        return _parse_token_response(resp)


class AsyncClientCredentialsAuth(httpx.Auth):
    """Async counterpart of :class:`ClientCredentialsAuth`.

    Same semantics (Bearer injection, proactive refresh under clock skew,
    single 401 retry) with asyncio-safe locking and non-blocking refresh I/O.
    """

    def __init__(
        self,
        token_url: str,
        client_id: str,
        client_secret: str,
        scopes: Sequence[str] = (),
        *,
        clock_skew: float = DEFAULT_CLOCK_SKEW,
        timeout: float = 10.0,
        verify_ssl: bool = True,
        transport: Optional[httpx.AsyncBaseTransport] = None,
    ) -> None:
        self._token_url = token_url
        self._client_id = client_id
        self._client_secret = client_secret
        self._scopes = tuple(scopes)
        self._clock_skew = clock_skew
        self._lock = asyncio.Lock()
        self._access_token: Optional[str] = None
        self._expires_at = 0.0
        self._http = httpx.AsyncClient(transport=transport, timeout=timeout, verify=verify_ssl)

    async def aclose(self) -> None:
        await self._http.aclose()

    async def __aenter__(self) -> "AsyncClientCredentialsAuth":
        return self

    async def __aexit__(self, *exc: object) -> None:
        await self.aclose()

    def auth_flow(self, request: httpx.Request):  # type: ignore[override]
        # httpx picks async_auth_flow for AsyncClient; landing here means the
        # auth was wired into a sync client by hand -- point at the sync class.
        raise RuntimeError(
            "AsyncClientCredentialsAuth only works with httpx.AsyncClient; "
            "use ClientCredentialsAuth for sync clients"
        )

    async def async_auth_flow(self, request: httpx.Request):  # type: ignore[override]
        request.headers["Authorization"] = "Bearer " + await self._get_token(force=False)
        response = yield request
        if response.status_code == 401:
            request.headers["Authorization"] = "Bearer " + await self._get_token(force=True)
            yield request

    async def _get_token(self, *, force: bool) -> str:
        async with self._lock:
            if (
                not force
                and self._access_token is not None
                and self._expires_at - self._clock_skew > time.time()
            ):
                return self._access_token
            token, lifetime = await self._request_token()
            self._access_token = token
            self._expires_at = time.time() + lifetime
            return token

    async def _request_token(self) -> Tuple[str, float]:
        resp = await self._http.post(
            self._token_url,
            data=_token_request_data(self._scopes),
            auth=(self._client_id, self._client_secret),
        )
        return _parse_token_response(resp)


def m2m_client(
    base_url: str,
    client_id: str,
    client_secret: str,
    scopes: Sequence[str] = (),
    *,
    timeout: float = 10.0,
    verify_ssl: bool = True,
    follow_redirects: bool = False,
    clock_skew: float = DEFAULT_CLOCK_SKEW,
    transport: Optional[httpx.BaseTransport] = None,
) -> AuthenticatedClient:
    """Build an :class:`AuthenticatedClient` with automatic client_credentials auth.

    Every request through the returned client carries a valid Bearer token:
    tokens are fetched lazily, cached, refreshed ``clock_skew`` seconds before
    expiry, and force-refreshed once on a 401. Example::

        from authforge import m2m_client
        from authforge.generated.api.o_auth_2 import get_oauth2_userinfo

        client = m2m_client("http://localhost:5555", "backend-svc", "s3cret",
                            scopes=["tokens:read"])
        userinfo = get_oauth2_userinfo.sync(client=client)
    """
    base = _normalize_base_url(base_url)
    auth = ClientCredentialsAuth(
        base + "/oauth2/token",
        client_id,
        client_secret,
        scopes,
        clock_skew=clock_skew,
        timeout=timeout,
        verify_ssl=verify_ssl,
        transport=transport,
    )
    http = httpx.Client(
        base_url=base,
        auth=auth,
        timeout=timeout,
        verify=verify_ssl,
        follow_redirects=follow_redirects,
        transport=transport,
    )
    client = AuthenticatedClient(base_url=base, token="", verify_ssl=verify_ssl, timeout=timeout)
    client.set_httpx_client(http)
    return client


def async_m2m_client(
    base_url: str,
    client_id: str,
    client_secret: str,
    scopes: Sequence[str] = (),
    *,
    timeout: float = 10.0,
    verify_ssl: bool = True,
    follow_redirects: bool = False,
    clock_skew: float = DEFAULT_CLOCK_SKEW,
    transport: Optional[httpx.AsyncBaseTransport] = None,
) -> AuthenticatedClient:
    """Async counterpart of :func:`m2m_client`."""
    base = _normalize_base_url(base_url)
    auth = AsyncClientCredentialsAuth(
        base + "/oauth2/token",
        client_id,
        client_secret,
        scopes,
        clock_skew=clock_skew,
        timeout=timeout,
        verify_ssl=verify_ssl,
        transport=transport,
    )
    http = httpx.AsyncClient(
        base_url=base,
        auth=auth,
        timeout=timeout,
        verify=verify_ssl,
        follow_redirects=follow_redirects,
        transport=transport,
    )
    client = AuthenticatedClient(base_url=base, token="", verify_ssl=verify_ssl, timeout=timeout)
    client.set_async_httpx_client(http)
    return client


def basic_auth_client(
    base_url: str,
    client_id: str,
    client_secret: str,
    *,
    timeout: float = 10.0,
    verify_ssl: bool = True,
    transport: Optional[httpx.BaseTransport] = None,
) -> AuthenticatedClient:
    """Client whose every request carries HTTP Basic client credentials.

    For endpoints that authenticate the CALLING CLIENT rather than a user
    bearer token -- ``POST /oauth2/introspect`` (RFC 7662 section 2.1),
    ``POST /oauth2/revoke`` -- where the credentials must travel via HTTP
    Basic (``client_secret_basic``; F-017: the body form is rejected).
    """
    base = _normalize_base_url(base_url)
    http = httpx.Client(
        base_url=base,
        headers={"Authorization": _basic_header(client_id, client_secret)},
        timeout=timeout,
        verify=verify_ssl,
        transport=transport,
    )
    client = AuthenticatedClient(base_url=base, token="", verify_ssl=verify_ssl, timeout=timeout)
    client.set_httpx_client(http)
    return client
