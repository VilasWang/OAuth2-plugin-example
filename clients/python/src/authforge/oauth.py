"""Authorization-code flow helpers (RFC 6749 §4.1 + PKCE RFC 7636).

Handwritten by design (client-sdk design D5): the browser round-trip cannot
and should not be automated by an SDK. What an SDK CAN own:

- building the ``/oauth2/authorize`` redirect URL (with PKCE S256 challenge
  and anti-CSRF ``state``),
- exchanging the returned code for tokens (confidential clients authenticate
  via HTTP Basic -- F-017; the body form is rejected),
- refreshing a rotating refresh token (V008 rotates on every use).

Example::

    from authforge import AuthorizationCodeFlow, PkcePair

    flow = AuthorizationCodeFlow(
        "http://localhost:5555", "my-client", "my-secret",
        redirect_uri="https://my.app/callback", scopes=["openid", "profile"],
    )
    pkce = PkcePair.generate()
    url = flow.build_authorize_url(state=session["csrf"], pkce=pkce)
    # ... send the user to `url`, receive ?code=... on the redirect_uri ...
    tokens = flow.exchange_code(code, pkce.verifier)
"""

from __future__ import annotations

import base64
import hashlib
import secrets
from dataclasses import dataclass
from typing import Optional, Sequence, Tuple
from urllib.parse import urlencode, urlparse

import httpx

from .generated.client import Client
from .generated.models.token_request import TokenRequest
from .generated.models.token_request_grant_type import TokenRequestGrantType
from .generated.models.token_response import TokenResponse

__all__ = ["PkcePair", "create_pkce_challenge", "AuthorizationCodeFlow"]


def create_pkce_challenge(verifier: str) -> str:
    """S256 code challenge (RFC 7636 §4.2): BASE64URL(SHA256(ASCII(verifier)))."""
    digest = hashlib.sha256(verifier.encode("ascii")).digest()
    return base64.urlsafe_b64encode(digest).decode("ascii").rstrip("=")


@dataclass(frozen=True)
class PkcePair:
    """A generated verifier + its S256 challenge."""

    verifier: str
    challenge: str

    @classmethod
    def generate(cls) -> "PkcePair":
        # RFC 7636 §4.1: 43-128 chars from the unreserved set. 64 chars of
        # url-safe base64 gives 384 bits of entropy -- well above the 256-bit
        # collision floor.
        verifier = secrets.token_urlsafe(48)
        return cls(verifier=verifier, challenge=create_pkce_challenge(verifier))


class AuthorizationCodeFlow:
    """Confidential/public client helper for the authorization-code grant."""

    def __init__(
        self,
        base_url: str,
        client_id: str,
        client_secret: Optional[str] = None,
        *,
        redirect_uri: Optional[str] = None,
        scopes: Sequence[str] = (),
        timeout: float = 10.0,
        verify_ssl: bool = True,
        transport: Optional[httpx.BaseTransport] = None,
    ) -> None:
        self._base_url = base_url.rstrip("/")
        self._client_id = client_id
        self._client_secret = client_secret
        self._redirect_uri = redirect_uri
        self._scopes = tuple(scopes)
        self._timeout = timeout
        self._verify_ssl = verify_ssl
        self._transport = transport

    @property
    def authorize_url(self) -> str:
        return self._base_url + "/oauth2/authorize"

    @property
    def token_url(self) -> str:
        return self._base_url + "/oauth2/token"

    def build_authorize_url(
        self,
        *,
        state: Optional[str] = None,
        pkce: Optional[PkcePair] = None,
        scopes: Optional[Sequence[str]] = None,
        redirect_uri: Optional[str] = None,
    ) -> str:
        """Build the GET /oauth2/authorize URL to send the user's browser to."""
        params = [
            ("response_type", "code"),
            ("client_id", self._client_id),
        ]
        effective_redirect = redirect_uri or self._redirect_uri
        if effective_redirect:
            params.append(("redirect_uri", effective_redirect))
        effective_scopes = self._scopes if scopes is None else tuple(scopes)
        if effective_scopes:
            params.append(("scope", " ".join(effective_scopes)))
        if state:
            params.append(("state", state))
        if pkce is not None:
            params.append(("code_challenge", pkce.challenge))
            params.append(("code_challenge_method", "S256"))
        return self.authorize_url + "?" + urlencode(params)

    def _token_http_client(self) -> httpx.Client:
        headers = {}
        if self._client_secret is not None:
            # F-017: confidential clients must use HTTP Basic on every token
            # request; the body form is rejected by the server.
            raw = f"{self._client_id}:{self._client_secret}".encode()
            headers["Authorization"] = "Basic " + base64.b64encode(raw).decode()
        return httpx.Client(
            base_url=self._base_url,
            headers=headers,
            timeout=self._timeout,
            verify=self._verify_ssl,
            transport=self._transport,
        )

    def _generated_client(self) -> Client:
        client = Client(base_url=self._base_url, verify_ssl=self._verify_ssl, timeout=self._timeout)
        client.set_httpx_client(self._token_http_client())
        return client

    def exchange_code(
        self,
        code: str,
        code_verifier: Optional[str] = None,
        *,
        redirect_uri: Optional[str] = None,
    ) -> TokenResponse:
        """POST /oauth2/token with grant_type=authorization_code.

        ``code_verifier`` is REQUIRED when the authorize request carried a
        code_challenge (the server enforces PKCE completion).
        """
        from .generated.api.o_auth_2 import post_oauth2_token

        request = TokenRequest(
            grant_type=TokenRequestGrantType.AUTHORIZATION_CODE,
            code=code,
            redirect_uri=redirect_uri or self._redirect_uri or "",
        )
        if code_verifier is not None:
            request.code_verifier = code_verifier
        if self._client_secret is None:
            # PUBLIC clients identify themselves in the form body (no secret).
            request.client_id = self._client_id
        with self._generated_client() as client:
            response = post_oauth2_token.sync_detailed(client=client, body=request)
        if response.status_code != 200 or response.parsed is None:
            raise self._auth_error(response.status_code, response.content)
        return response.parsed

    def refresh(self, refresh_token: str, *, scopes: Optional[Sequence[str]] = None) -> TokenResponse:
        """POST /oauth2/token with grant_type=refresh_token.

        AuthForge rotates refresh tokens on every use (migration V008): always
        take the new ``refresh_token`` from the returned response.
        """
        from .generated.api.o_auth_2 import post_oauth2_token

        request = TokenRequest(grant_type=TokenRequestGrantType.REFRESH_TOKEN, refresh_token=refresh_token)
        if self._client_secret is None:
            request.client_id = self._client_id
        with self._generated_client() as client:
            response = post_oauth2_token.sync_detailed(client=client, body=request)
        if response.status_code != 200 or response.parsed is None:
            raise self._auth_error(response.status_code, response.content)
        return response.parsed

    @staticmethod
    def _auth_error(status_code: int, body: bytes) -> Exception:
        try:
            parsed = httpx.Response(status_code, content=body).json()
            error = parsed.get("error", "invalid_response")
            description = parsed.get("error_description")
        except ValueError:
            error, description = "invalid_response", None
        from .m2m import AuthForgeAuthError

        return AuthForgeAuthError(error, description, status_code)


def parse_authorization_response(redirect_url: str) -> Tuple[Optional[str], Optional[str], Optional[str]]:
    """Extract ``(code, state, error)`` from the redirect_uri query string.

    The caller MUST verify ``state`` matches what it sent before trusting
    ``code`` (anti-CSRF, RFC 6749 §10.12).
    """
    query = urlparse(redirect_url).query
    from urllib.parse import parse_qs

    params = parse_qs(query)
    code = params.get("code", [None])[0]
    state = params.get("state", [None])[0]
    error = params.get("error", [None])[0]
    return code, state, error
