from http import HTTPStatus
from typing import Any, cast

import httpx

from ... import errors
from ...client import AuthenticatedClient, Client
from ...models.o_auth_2_error import OAuth2Error
from ...models.post_oauth_2_revoke_body import PostOauth2RevokeBody
from ...types import Response


def _get_kwargs(
    *,
    body: PostOauth2RevokeBody,
) -> dict[str, Any]:
    headers: dict[str, Any] = {}

    _kwargs: dict[str, Any] = {
        "method": "post",
        "url": "/oauth2/revoke",
    }

    _kwargs["data"] = body.to_dict()
    headers["Content-Type"] = "application/x-www-form-urlencoded"

    _kwargs["headers"] = headers
    return _kwargs


def _parse_response(*, client: AuthenticatedClient | Client, response: httpx.Response) -> Any | OAuth2Error | None:
    if response.status_code == 200:
        response_200 = cast(Any, None)
        return response_200

    if response.status_code == 400:
        response_400 = OAuth2Error.from_dict(response.json())

        return response_400

    if response.status_code == 401:
        response_401 = OAuth2Error.from_dict(response.json())

        return response_401

    if response.status_code == 429:
        response_429 = cast(Any, None)
        return response_429

    if client.raise_on_unexpected_status:
        raise errors.UnexpectedStatus(response.status_code, response.content)
    else:
        return None


def _build_response(*, client: AuthenticatedClient | Client, response: httpx.Response) -> Response[Any | OAuth2Error]:
    return Response(
        status_code=HTTPStatus(response.status_code),
        content=response.content,
        headers=response.headers,
        parsed=_parse_response(client=client, response=response),
    )


def sync_detailed(
    *,
    client: AuthenticatedClient,
    body: PostOauth2RevokeBody,
) -> Response[Any | OAuth2Error]:
    """Revoke token

     RFC 7009 OAuth 2.0 Token Revocation. Revokes an access OR refresh token (C3 fix — refresh tokens are
    now actually revoked, not silently no-op'd). The caller must be the token's owning client (ownership
    enforced via introspection). CONFIDENTIAL clients authenticate via client_secret (HTTP Basic or
    body); PUBLIC clients (token_endpoint_auth_method='none') authenticate with client_id only (RFC 7009
    §2.1 exempts them from client authentication at this endpoint). Unknown or already-inactive tokens
    still return 200 per RFC 7009; the success body is EMPTY.

    Args:
        body (PostOauth2RevokeBody):

    Raises:
        errors.UnexpectedStatus: If the server returns an undocumented status code and Client.raise_on_unexpected_status is True.
        httpx.TimeoutException: If the request takes longer than Client.timeout.

    Returns:
        Response[Any | OAuth2Error]
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
    client: AuthenticatedClient,
    body: PostOauth2RevokeBody,
) -> Any | OAuth2Error | None:
    """Revoke token

     RFC 7009 OAuth 2.0 Token Revocation. Revokes an access OR refresh token (C3 fix — refresh tokens are
    now actually revoked, not silently no-op'd). The caller must be the token's owning client (ownership
    enforced via introspection). CONFIDENTIAL clients authenticate via client_secret (HTTP Basic or
    body); PUBLIC clients (token_endpoint_auth_method='none') authenticate with client_id only (RFC 7009
    §2.1 exempts them from client authentication at this endpoint). Unknown or already-inactive tokens
    still return 200 per RFC 7009; the success body is EMPTY.

    Args:
        body (PostOauth2RevokeBody):

    Raises:
        errors.UnexpectedStatus: If the server returns an undocumented status code and Client.raise_on_unexpected_status is True.
        httpx.TimeoutException: If the request takes longer than Client.timeout.

    Returns:
        Any | OAuth2Error
    """

    return sync_detailed(
        client=client,
        body=body,
    ).parsed


async def asyncio_detailed(
    *,
    client: AuthenticatedClient,
    body: PostOauth2RevokeBody,
) -> Response[Any | OAuth2Error]:
    """Revoke token

     RFC 7009 OAuth 2.0 Token Revocation. Revokes an access OR refresh token (C3 fix — refresh tokens are
    now actually revoked, not silently no-op'd). The caller must be the token's owning client (ownership
    enforced via introspection). CONFIDENTIAL clients authenticate via client_secret (HTTP Basic or
    body); PUBLIC clients (token_endpoint_auth_method='none') authenticate with client_id only (RFC 7009
    §2.1 exempts them from client authentication at this endpoint). Unknown or already-inactive tokens
    still return 200 per RFC 7009; the success body is EMPTY.

    Args:
        body (PostOauth2RevokeBody):

    Raises:
        errors.UnexpectedStatus: If the server returns an undocumented status code and Client.raise_on_unexpected_status is True.
        httpx.TimeoutException: If the request takes longer than Client.timeout.

    Returns:
        Response[Any | OAuth2Error]
    """

    kwargs = _get_kwargs(
        body=body,
    )

    response = await client.get_async_httpx_client().request(**kwargs)

    return _build_response(client=client, response=response)


async def asyncio(
    *,
    client: AuthenticatedClient,
    body: PostOauth2RevokeBody,
) -> Any | OAuth2Error | None:
    """Revoke token

     RFC 7009 OAuth 2.0 Token Revocation. Revokes an access OR refresh token (C3 fix — refresh tokens are
    now actually revoked, not silently no-op'd). The caller must be the token's owning client (ownership
    enforced via introspection). CONFIDENTIAL clients authenticate via client_secret (HTTP Basic or
    body); PUBLIC clients (token_endpoint_auth_method='none') authenticate with client_id only (RFC 7009
    §2.1 exempts them from client authentication at this endpoint). Unknown or already-inactive tokens
    still return 200 per RFC 7009; the success body is EMPTY.

    Args:
        body (PostOauth2RevokeBody):

    Raises:
        errors.UnexpectedStatus: If the server returns an undocumented status code and Client.raise_on_unexpected_status is True.
        httpx.TimeoutException: If the request takes longer than Client.timeout.

    Returns:
        Any | OAuth2Error
    """

    return (
        await asyncio_detailed(
            client=client,
            body=body,
        )
    ).parsed
