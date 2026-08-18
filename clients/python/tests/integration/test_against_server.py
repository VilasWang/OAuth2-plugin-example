"""Integration tests against a running AuthForge full stack (matrix I1-I5).

Skipped unless AUTHFORGE_BASE_URL is set. Boot recipe (Windows):
    scripts/backend/setup_database.bat     # migrations + seeds (backend-svc)
    ...start authforge-server.exe, poll /health/live...
See docs/productization-evolution/in-progress/client-sdk-implementation-plan.md
section 4 for the full server-boot procedure.
"""

from __future__ import annotations

import os
from typing import Optional

import httpx
import pytest

from authforge import (
    AuthForgeAuthError,
    AuthorizationCodeFlow,
    PkcePair,
    basic_auth_client,
    fetch_client_credentials_token,
    m2m_client,
)
from authforge.generated.api.o_auth_2 import post_oauth2_introspect
from authforge.generated.api.open_id_connect import get_well_known_openid_configuration
from authforge.generated.models.post_oauth_2_introspect_body import PostOauth2IntrospectBody

BASE_URL = os.environ.get("AUTHFORGE_BASE_URL", "")
CLIENT_ID = os.environ.get("AUTHFORGE_CLIENT_ID", "backend-svc")
CLIENT_SECRET = os.environ.get("AUTHFORGE_CLIENT_SECRET", "test-secret")

pytestmark = pytest.mark.skipif(
    not BASE_URL, reason="AUTHFORGE_BASE_URL not set; integration tests need a running stack"
)


def test_i1_client_credentials_token() -> None:
    token, expires_in = fetch_client_credentials_token(
        BASE_URL, CLIENT_ID, CLIENT_SECRET, ["tokens:read"]
    )
    assert token
    assert expires_in > 0


def test_i2_introspect_own_token() -> None:
    """I2 -- freshly minted token introspects as active via the generated client."""
    token, _ = fetch_client_credentials_token(BASE_URL, CLIENT_ID, CLIENT_SECRET, ["tokens:read"])
    client = basic_auth_client(BASE_URL, CLIENT_ID, CLIENT_SECRET)
    result = post_oauth2_introspect.sync(client=client, body=PostOauth2IntrospectBody(token=token))
    assert result.active is True
    assert result.client_id == CLIENT_ID
    assert result.scope == "tokens:read"


def test_i3_discovery() -> None:
    """I3 -- discovery doc served; issuer is the hardcoded fallback unless
    metadata.issuer is configured in the server config."""
    client = basic_auth_client(BASE_URL, CLIENT_ID, CLIENT_SECRET)
    doc = get_well_known_openid_configuration.sync(client=client)
    assert doc.issuer
    # DiscoveryController falls back to http://localhost:5555 when the config
    # does not set metadata.issuer; either way the port must be our test port.
    assert doc.issuer.endswith(":5555"), doc.issuer
    assert doc.token_endpoint.endswith("/oauth2/token")
    assert doc.introspection_endpoint.endswith("/oauth2/introspect")


def test_i4_m2m_token_userinfo_rejected() -> None:
    """I4 -- M2M tokens have no user identity: userinfo must 401."""
    client = m2m_client(BASE_URL, CLIENT_ID, CLIENT_SECRET, ["tokens:read"])
    try:
        from authforge.generated.api.o_auth_2 import get_oauth2_userinfo

        response = get_oauth2_userinfo.sync_detailed(client=client)
        assert response.status_code == 401
        assert response.parsed is not None
        assert response.parsed.error == "invalid_token"
    finally:
        client.get_httpx_client().close()


def test_i5_wrong_secret_raises() -> None:
    """I5 -- bad credentials surface as AuthForgeAuthError(invalid_client)."""
    with pytest.raises(AuthForgeAuthError) as excinfo:
        fetch_client_credentials_token(BASE_URL, CLIENT_ID, "definitely-wrong", ["tokens:read"])
    assert excinfo.value.error == "invalid_client"
    assert excinfo.value.status_code == 401
