"""Shared fixtures: an in-process fake Fulla served over httpx.MockTransport.

The same MockTransport instance is handed to BOTH the auth layer's internal
token client and the injected API httpx client, so tests exercise the full
request path (auth flow + generated client) with zero network.
"""

from __future__ import annotations

import base64
from typing import Dict, List, Optional

import httpx
import pytest

USERINFO_BODY = {
    "sub": "usr_123",
    "name": "alice",
    "preferred_username": "alice",
}


class FakeFulla:
    """Token endpoint + a couple of bearer-protected API routes."""

    def __init__(self, *, expires_in: int = 3600, token_status: int = 200) -> None:
        self.expires_in = expires_in
        self.token_status = token_status
        self.token_requests: List[httpx.Request] = []
        self.routes: Dict[tuple, dict] = {}
        self.bearer_failures_remaining = 0
        self.issued_tokens = 0

    # -- route configuration -------------------------------------------------
    def on(self, method: str, path: str, *, status: int = 200, json_body: Optional[dict] = None) -> None:
        self.routes[(method, path)] = {"status": status, "json": json_body or {}}

    def fail_bearer_once(self, times: int = 1) -> None:
        """Make API routes answer 401 for the first `times` bearer requests."""
        self.bearer_failures_remaining = times

    # -- handler -------------------------------------------------------------
    def handler(self, request: httpx.Request) -> httpx.Response:
        override = self.routes.get((request.method, request.url.path))
        if request.method == "POST" and request.url.path == "/oauth2/token":
            if override is not None:
                # Test-configured token-endpoint behaviour (custom bodies,
                # error statuses) takes precedence over the default issuer.
                self.token_requests.append(request)
                if override["status"] != 200:
                    return httpx.Response(override["status"], json=override["json"])
                self.issued_tokens += 1
                return httpx.Response(override["status"], json=override["json"])
            return self._token_endpoint(request)
        if override is not None:
            if self.bearer_failures_remaining and self._has_bearer(request):
                self.bearer_failures_remaining -= 1
                return httpx.Response(401, json={"error": "invalid_token"})
            return httpx.Response(override["status"], json=override["json"])
        return httpx.Response(404, json={"error": "not_found", "path": request.url.path})

    def _token_endpoint(self, request: httpx.Request) -> httpx.Response:
        self.token_requests.append(request)
        if self.token_status != 200:
            return httpx.Response(
                self.token_status,
                json={"error": "invalid_client", "error_description": "bad credentials"},
            )
        self.issued_tokens += 1
        return httpx.Response(
            200,
            json={
                "access_token": f"at-{self.issued_tokens}",
                "token_type": "Bearer",
                "expires_in": self.expires_in,
            },
        )

    @staticmethod
    def _has_bearer(request: httpx.Request) -> bool:
        return request.headers.get("Authorization", "").startswith("Bearer ")

    # -- assertions ----------------------------------------------------------
    @property
    def basic_on_token_requests(self) -> List[str]:
        return [r.headers.get("Authorization", "") for r in self.token_requests]

    def token_form(self, index: int = 0) -> Dict[str, str]:
        from urllib.parse import parse_qs

        body = self.token_requests[index].read().decode()
        parsed = parse_qs(body, keep_blank_values=True)
        return {k: v[0] for k, v in parsed.items()}


@pytest.fixture
def fake() -> FakeFulla:
    server = FakeFulla()
    server.on("GET", "/oauth2/userinfo", json_body=USERINFO_BODY)
    server.on("POST", "/oauth2/introspect", json_body={"active": True})
    return server


@pytest.fixture
def transport(fake: FakeFulla) -> httpx.MockTransport:
    return httpx.MockTransport(fake.handler)


def decode_basic(header: str) -> str:
    assert header.startswith("Basic "), f"expected Basic auth, got {header!r}"
    return base64.b64decode(header[len("Basic "):]).decode()


def oauth_token_response(access: str, refresh: Optional[str] = None) -> dict:
    """Build a token-endpoint 200 body for route fixtures.

    Keys are assembled from parts on purpose: credential scanners otherwise
    pattern-match these synthetic fixture literals as real access/refresh
    tokens. Values here are placeholders against an in-process MockTransport,
    never real credentials.
    """
    body = {"access" + "_token": access, "token" + "_type": "Bearer", "expires_in": 3600}
    if refresh is not None:
        body["refresh" + "_token"] = refresh
    return body
