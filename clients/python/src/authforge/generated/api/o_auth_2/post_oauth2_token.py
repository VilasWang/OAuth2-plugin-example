from http import HTTPStatus
from typing import Any

import httpx

from ... import errors
from ...client import AuthenticatedClient, Client
from ...models.o_auth_2_error import OAuth2Error
from ...models.token_request import TokenRequest
from ...models.token_response import TokenResponse
from ...types import Response


def _get_kwargs(
    *,
    body: TokenRequest,
) -> dict[str, Any]:
    headers: dict[str, Any] = {}

    _kwargs: dict[str, Any] = {
        "method": "post",
        "url": "/oauth2/token",
    }

    _kwargs["data"] = body.to_dict()
    headers["Content-Type"] = "application/x-www-form-urlencoded"

    _kwargs["headers"] = headers
    return _kwargs


def _parse_response(
    *, client: AuthenticatedClient | Client, response: httpx.Response
) -> OAuth2Error | TokenResponse | None:
    if response.status_code == 200:
        response_200 = TokenResponse.from_dict(response.json())

        return response_200

    if response.status_code == 400:
        response_400 = OAuth2Error.from_dict(response.json())

        return response_400

    if response.status_code == 401:
        response_401 = OAuth2Error.from_dict(response.json())

        return response_401

    if response.status_code == 429:
        response_429 = OAuth2Error.from_dict(response.json())

        return response_429

    if client.raise_on_unexpected_status:
        raise errors.UnexpectedStatus(response.status_code, response.content)
    else:
        return None


def _build_response(
    *, client: AuthenticatedClient | Client, response: httpx.Response
) -> Response[OAuth2Error | TokenResponse]:
    return Response(
        status_code=HTTPStatus(response.status_code),
        content=response.content,
        headers=response.headers,
        parsed=_parse_response(client=client, response=response),
    )


def sync_detailed(
    *,
    client: AuthenticatedClient | Client,
    body: TokenRequest,
) -> Response[OAuth2Error | TokenResponse]:
    """Exchange authorization code for access token

     OAuth2 token endpoint - exchanges an authorization code, refresh token, client credentials, or
    device code for access tokens (RFC 6749 §3.2). Request parameters are sent as application/x-www-
    form-urlencoded (the server also accepts them as query parameters). CONFIDENTIAL clients
    authenticate via HTTP Basic (preferred) or the client_id/client_secret form fields; PUBLIC clients
    send client_id only.

    Args:
        body (TokenRequest): RFC 6749 §4.1.3 token request (form-encoded). Field requirements are
            grant-dependent: authorization_code needs code (+ redirect_uri + code_verifier when PKCE
            was used); refresh_token needs refresh_token; client_credentials may send scope;
            device_code needs device_code. CONFIDENTIAL clients authenticate via HTTP Basic or
            client_id + client_secret fields.

    Raises:
        errors.UnexpectedStatus: If the server returns an undocumented status code and Client.raise_on_unexpected_status is True.
        httpx.TimeoutException: If the request takes longer than Client.timeout.

    Returns:
        Response[OAuth2Error | TokenResponse]
    """

    kwargs = _get_kwargs(
        body=body,
    )

    response = client.get_httpx_client().request(
        **kwargs,
    )

    return _build_response(client=client, response=response)


def sync(
    *,
    client: AuthenticatedClient | Client,
    body: TokenRequest,
) -> OAuth2Error | TokenResponse | None:
    """Exchange authorization code for access token

     OAuth2 token endpoint - exchanges an authorization code, refresh token, client credentials, or
    device code for access tokens (RFC 6749 §3.2). Request parameters are sent as application/x-www-
    form-urlencoded (the server also accepts them as query parameters). CONFIDENTIAL clients
    authenticate via HTTP Basic (preferred) or the client_id/client_secret form fields; PUBLIC clients
    send client_id only.

    Args:
        body (TokenRequest): RFC 6749 §4.1.3 token request (form-encoded). Field requirements are
            grant-dependent: authorization_code needs code (+ redirect_uri + code_verifier when PKCE
            was used); refresh_token needs refresh_token; client_credentials may send scope;
            device_code needs device_code. CONFIDENTIAL clients authenticate via HTTP Basic or
            client_id + client_secret fields.

    Raises:
        errors.UnexpectedStatus: If the server returns an undocumented status code and Client.raise_on_unexpected_status is True.
        httpx.TimeoutException: If the request takes longer than Client.timeout.

    Returns:
        OAuth2Error | TokenResponse
    """

    return sync_detailed(
        client=client,
        body=body,
    ).parsed


async def asyncio_detailed(
    *,
    client: AuthenticatedClient | Client,
    body: TokenRequest,
) -> Response[OAuth2Error | TokenResponse]:
    """Exchange authorization code for access token

     OAuth2 token endpoint - exchanges an authorization code, refresh token, client credentials, or
    device code for access tokens (RFC 6749 §3.2). Request parameters are sent as application/x-www-
    form-urlencoded (the server also accepts them as query parameters). CONFIDENTIAL clients
    authenticate via HTTP Basic (preferred) or the client_id/client_secret form fields; PUBLIC clients
    send client_id only.

    Args:
        body (TokenRequest): RFC 6749 §4.1.3 token request (form-encoded). Field requirements are
            grant-dependent: authorization_code needs code (+ redirect_uri + code_verifier when PKCE
            was used); refresh_token needs refresh_token; client_credentials may send scope;
            device_code needs device_code. CONFIDENTIAL clients authenticate via HTTP Basic or
            client_id + client_secret fields.

    Raises:
        errors.UnexpectedStatus: If the server returns an undocumented status code and Client.raise_on_unexpected_status is True.
        httpx.TimeoutException: If the request takes longer than Client.timeout.

    Returns:
        Response[OAuth2Error | TokenResponse]
    """

    kwargs = _get_kwargs(
        body=body,
    )

    response = await client.get_async_httpx_client().request(**kwargs)

    return _build_response(client=client, response=response)


async def asyncio(
    *,
    client: AuthenticatedClient | Client,
    body: TokenRequest,
) -> OAuth2Error | TokenResponse | None:
    """Exchange authorization code for access token

     OAuth2 token endpoint - exchanges an authorization code, refresh token, client credentials, or
    device code for access tokens (RFC 6749 §3.2). Request parameters are sent as application/x-www-
    form-urlencoded (the server also accepts them as query parameters). CONFIDENTIAL clients
    authenticate via HTTP Basic (preferred) or the client_id/client_secret form fields; PUBLIC clients
    send client_id only.

    Args:
        body (TokenRequest): RFC 6749 §4.1.3 token request (form-encoded). Field requirements are
            grant-dependent: authorization_code needs code (+ redirect_uri + code_verifier when PKCE
            was used); refresh_token needs refresh_token; client_credentials may send scope;
            device_code needs device_code. CONFIDENTIAL clients authenticate via HTTP Basic or
            client_id + client_secret fields.

    Raises:
        errors.UnexpectedStatus: If the server returns an undocumented status code and Client.raise_on_unexpected_status is True.
        httpx.TimeoutException: If the request takes longer than Client.timeout.

    Returns:
        OAuth2Error | TokenResponse
    """

    return (
        await asyncio_detailed(
            client=client,
            body=body,
        )
    ).parsed
