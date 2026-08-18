"""Unit tests for the client_credentials auth layer (matrix P1-P5, P8)."""

from __future__ import annotations

import httpx
import pytest
from conftest import USERINFO_BODY, decode_basic

from authforge import (
    AsyncClientCredentialsAuth,
    AuthForgeAuthError,
    ClientCredentialsAuth,
    async_m2m_client,
    basic_auth_client,
    fetch_client_credentials_token,
    m2m_client,
)
from authforge.generated.api.o_auth_2 import get_oauth2_userinfo, post_oauth2_introspect
from authforge.generated.models.post_oauth_2_introspect_body import PostOauth2IntrospectBody

BASE = "http://server.test"


class TestTokenFetch:
    """P1 -- one-shot fetch: Basic auth shape + form fields."""

    def test_basic_auth_and_form_fields(self, fake, transport):
        token, expires_in = fetch_client_credentials_token(
            BASE, "backend-svc", "test-secret", ["tokens:read", "tokens:write"],
            transport=transport,
        )
        assert token == "at-1"
        assert expires_in == 3600
        assert len(fake.token_requests) == 1
        assert decode_basic(fake.basic_on_token_requests[0]) == "backend-svc:test-secret"
        form = fake.token_form()
        assert form["grant_type"] == "client_credentials"
        assert form["scope"] == "tokens:read tokens:write"  # space-joined

    def test_no_scope_omits_field(self, fake, transport):
        fetch_client_credentials_token(BASE, "c", "s", transport=transport)
        assert "scope" not in fake.token_form()

    def test_error_propagation(self, fake, transport):
        """P5 -- RFC 6749 error body surfaces as AuthForgeAuthError."""
        fake.token_status = 401
        with pytest.raises(AuthForgeAuthError) as excinfo:
            fetch_client_credentials_token(BASE, "c", "bad", transport=transport)
        assert excinfo.value.error == "invalid_client"
        assert excinfo.value.error_description == "bad credentials"
        assert excinfo.value.status_code == 401


class TestBearerInjectionAndCache:
    """P2 -- Bearer injected on every API request; token fetched once."""

    def test_bearer_and_cache(self, fake, transport):
        client = m2m_client(BASE, "backend-svc", "test-secret", transport=transport)
        try:
            first = get_oauth2_userinfo.sync(client=client)
            second = get_oauth2_userinfo.sync(client=client)
            assert first.sub == second.sub == USERINFO_BODY["sub"]
            assert len(fake.token_requests) == 1  # cached across calls
        finally:
            client.get_httpx_client().close()


class TestProactiveRefresh:
    """P3 -- token is refreshed before the advertised expiry (clock skew)."""

    def test_short_lived_token_refreshes(self, fake, transport):
        fake.expires_in = 10  # < DEFAULT_CLOCK_SKEW (30s) -> always stale
        client = m2m_client(BASE, "c", "s", transport=transport)
        try:
            get_oauth2_userinfo.sync(client=client)
            get_oauth2_userinfo.sync(client=client)
            assert len(fake.token_requests) == 2  # proactive refresh happened
        finally:
            client.get_httpx_client().close()


class Test401Retry:
    """P4 -- one forced refresh + retry on 401; second 401 passes through."""

    def test_retry_once_on_401(self, fake, transport):
        client = m2m_client(BASE, "c", "s", transport=transport)
        try:
            get_oauth2_userinfo.sync(client=client)  # primes the cache
            fake.fail_bearer_once(times=1)
            result = get_oauth2_userinfo.sync(client=client)
            assert result.sub == USERINFO_BODY["sub"]
            assert len(fake.token_requests) == 2  # forced refresh
        finally:
            client.get_httpx_client().close()

    def test_retry_resends_form_body(self, fake, transport):
        """P4 (body case) -- the retried POST carries the same form body."""
        client = m2m_client(BASE, "c", "s", transport=transport)
        try:
            get_oauth2_userinfo.sync(client=client)  # prime token cache
            fake.fail_bearer_once(times=1)
            result = post_oauth2_introspect.sync(
                client=client, body=PostOauth2IntrospectBody(token="tok-xyz")
            )
            assert result.active is True
            assert len(fake.token_requests) == 2
        finally:
            client.get_httpx_client().close()

    def test_second_401_passes_through(self, fake, transport):
        fake.on("GET", "/oauth2/userinfo", status=401, json_body={"error": "invalid_token"})
        client = m2m_client(BASE, "c", "s", transport=transport)
        try:
            response = get_oauth2_userinfo.sync_detailed(client=client)
            assert response.status_code == 401  # one retry, then pass-through
            assert len(fake.token_requests) == 2
        finally:
            client.get_httpx_client().close()


class TestBasicAuthClient:
    """introspect/revoke style: HTTP Basic on every request, no bearer logic."""

    def test_every_request_carries_basic(self, fake, transport):
        client = basic_auth_client(BASE, "backend-svc", "test-secret", transport=transport)
        try:
            result = post_oauth2_introspect.sync(
                client=client, body=PostOauth2IntrospectBody(token="tok-xyz")
            )
            assert result.active is True
            assert fake.token_requests == []  # never talks to the token endpoint
        finally:
            client.get_httpx_client().close()


class TestAsyncEquivalence:
    """P8 -- the async auth mirrors P1/P2/P3/P4 semantics."""

    async def test_fetch_bearer_cache(self, fake, transport):
        client = async_m2m_client(BASE, "c", "s", transport=transport)
        try:
            first = await get_oauth2_userinfo.asyncio(client=client)
            second = await get_oauth2_userinfo.asyncio(client=client)
            assert first.sub == second.sub == USERINFO_BODY["sub"]
            assert len(fake.token_requests) == 1
            assert decode_basic(fake.basic_on_token_requests[0]) == "c:s"
        finally:
            await client.get_async_httpx_client().aclose()

    async def test_proactive_refresh(self, fake, transport):
        fake.expires_in = 10
        client = async_m2m_client(BASE, "c", "s", transport=transport)
        try:
            await get_oauth2_userinfo.asyncio(client=client)
            await get_oauth2_userinfo.asyncio(client=client)
            assert len(fake.token_requests) == 2
        finally:
            await client.get_async_httpx_client().aclose()

    async def test_401_retry_once(self, fake, transport):
        client = async_m2m_client(BASE, "c", "s", transport=transport)
        try:
            await get_oauth2_userinfo.asyncio(client=client)
            fake.fail_bearer_once(times=1)
            result = await get_oauth2_userinfo.asyncio(client=client)
            assert result.sub == USERINFO_BODY["sub"]
            assert len(fake.token_requests) == 2
        finally:
            await client.get_async_httpx_client().aclose()

    async def test_error_propagation(self, fake, transport):
        fake.token_status = 401
        with pytest.raises(AuthForgeAuthError) as excinfo:
            client = async_m2m_client(BASE, "c", "bad", transport=transport)
            try:
                await get_oauth2_userinfo.asyncio(client=client)
            finally:
                await client.get_async_httpx_client().aclose()
        assert excinfo.value.error == "invalid_client"


class TestAuthClassesCloseCleanly:
    def test_sync_context_manager(self, transport):
        with ClientCredentialsAuth(BASE + "/oauth2/token", "c", "s", transport=transport) as auth:
            assert auth._get_token(force=False) == "at-1"

    async def test_async_context_manager(self, transport):
        async with AsyncClientCredentialsAuth(BASE + "/oauth2/token", "c", "s", transport=transport) as auth:
            assert await auth._get_token(force=False) == "at-1"
