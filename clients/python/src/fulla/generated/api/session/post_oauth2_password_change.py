from http import HTTPStatus
from typing import Any

import httpx

from ... import errors
from ...client import AuthenticatedClient, Client
from ...models.error_envelope import ErrorEnvelope
from ...models.message_response import MessageResponse
from ...models.post_oauth_2_password_change_body import PostOauth2PasswordChangeBody
from ...types import Response


def _get_kwargs(
    *,
    body: PostOauth2PasswordChangeBody,
) -> dict[str, Any]:
    headers: dict[str, Any] = {}

    _kwargs: dict[str, Any] = {
        "method": "post",
        "url": "/oauth2/password/change",
    }

    _kwargs["json"] = body.to_dict()

    headers["Content-Type"] = "application/json"

    _kwargs["headers"] = headers
    return _kwargs


def _parse_response(
    *, client: AuthenticatedClient | Client, response: httpx.Response
) -> ErrorEnvelope | MessageResponse | None:
    if response.status_code == 200:
        response_200 = MessageResponse.from_dict(response.json())

        return response_200

    if response.status_code == 400:
        response_400 = ErrorEnvelope.from_dict(response.json())

        return response_400

    if response.status_code == 401:
        response_401 = ErrorEnvelope.from_dict(response.json())

        return response_401

    if response.status_code == 500:
        response_500 = ErrorEnvelope.from_dict(response.json())

        return response_500

    if client.raise_on_unexpected_status:
        raise errors.UnexpectedStatus(response.status_code, response.content)
    else:
        return None


def _build_response(
    *, client: AuthenticatedClient | Client, response: httpx.Response
) -> Response[ErrorEnvelope | MessageResponse]:
    return Response(
        status_code=HTTPStatus(response.status_code),
        content=response.content,
        headers=response.headers,
        parsed=_parse_response(client=client, response=response),
    )


def sync_detailed(
    *,
    client: AuthenticatedClient | Client,
    body: PostOauth2PasswordChangeBody,
) -> Response[ErrorEnvelope | MessageResponse]:
    """Change Password (Forced First-Login Flow)

     Changes the password of the session user while the account is flagged must_change_password (#145:
    bootstrap admin, admin-created users). Session-authenticated like /oauth2/login (no Bearer token — a
    flagged account cannot obtain tokens by design); requires old_password, applies the
    auth.min_password_length policy and a 128-character maximum, clears the flag, revokes all
    access/refresh tokens, and demotes the session to anonymous (sign in again). Only usable while the
    session carries the must_change_password marker set at login. JSON body only — the endpoint has no
    CSRF nonce of its own, so the content type (which a cross-site form cannot produce) plus the
    old_password knowledge factor close the cross-site surface.

    Args:
        body (PostOauth2PasswordChangeBody):

    Raises:
        errors.UnexpectedStatus: If the server returns an undocumented status code and Client.raise_on_unexpected_status is True.
        httpx.TimeoutException: If the request takes longer than Client.timeout.

    Returns:
        Response[ErrorEnvelope | MessageResponse]
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
    body: PostOauth2PasswordChangeBody,
) -> ErrorEnvelope | MessageResponse | None:
    """Change Password (Forced First-Login Flow)

     Changes the password of the session user while the account is flagged must_change_password (#145:
    bootstrap admin, admin-created users). Session-authenticated like /oauth2/login (no Bearer token — a
    flagged account cannot obtain tokens by design); requires old_password, applies the
    auth.min_password_length policy and a 128-character maximum, clears the flag, revokes all
    access/refresh tokens, and demotes the session to anonymous (sign in again). Only usable while the
    session carries the must_change_password marker set at login. JSON body only — the endpoint has no
    CSRF nonce of its own, so the content type (which a cross-site form cannot produce) plus the
    old_password knowledge factor close the cross-site surface.

    Args:
        body (PostOauth2PasswordChangeBody):

    Raises:
        errors.UnexpectedStatus: If the server returns an undocumented status code and Client.raise_on_unexpected_status is True.
        httpx.TimeoutException: If the request takes longer than Client.timeout.

    Returns:
        ErrorEnvelope | MessageResponse
    """

    return sync_detailed(
        client=client,
        body=body,
    ).parsed


async def asyncio_detailed(
    *,
    client: AuthenticatedClient | Client,
    body: PostOauth2PasswordChangeBody,
) -> Response[ErrorEnvelope | MessageResponse]:
    """Change Password (Forced First-Login Flow)

     Changes the password of the session user while the account is flagged must_change_password (#145:
    bootstrap admin, admin-created users). Session-authenticated like /oauth2/login (no Bearer token — a
    flagged account cannot obtain tokens by design); requires old_password, applies the
    auth.min_password_length policy and a 128-character maximum, clears the flag, revokes all
    access/refresh tokens, and demotes the session to anonymous (sign in again). Only usable while the
    session carries the must_change_password marker set at login. JSON body only — the endpoint has no
    CSRF nonce of its own, so the content type (which a cross-site form cannot produce) plus the
    old_password knowledge factor close the cross-site surface.

    Args:
        body (PostOauth2PasswordChangeBody):

    Raises:
        errors.UnexpectedStatus: If the server returns an undocumented status code and Client.raise_on_unexpected_status is True.
        httpx.TimeoutException: If the request takes longer than Client.timeout.

    Returns:
        Response[ErrorEnvelope | MessageResponse]
    """

    kwargs = _get_kwargs(
        body=body,
    )

    response = await client.get_async_httpx_client().request(**kwargs)

    return _build_response(client=client, response=response)


async def asyncio(
    *,
    client: AuthenticatedClient | Client,
    body: PostOauth2PasswordChangeBody,
) -> ErrorEnvelope | MessageResponse | None:
    """Change Password (Forced First-Login Flow)

     Changes the password of the session user while the account is flagged must_change_password (#145:
    bootstrap admin, admin-created users). Session-authenticated like /oauth2/login (no Bearer token — a
    flagged account cannot obtain tokens by design); requires old_password, applies the
    auth.min_password_length policy and a 128-character maximum, clears the flag, revokes all
    access/refresh tokens, and demotes the session to anonymous (sign in again). Only usable while the
    session carries the must_change_password marker set at login. JSON body only — the endpoint has no
    CSRF nonce of its own, so the content type (which a cross-site form cannot produce) plus the
    old_password knowledge factor close the cross-site surface.

    Args:
        body (PostOauth2PasswordChangeBody):

    Raises:
        errors.UnexpectedStatus: If the server returns an undocumented status code and Client.raise_on_unexpected_status is True.
        httpx.TimeoutException: If the request takes longer than Client.timeout.

    Returns:
        ErrorEnvelope | MessageResponse
    """

    return (
        await asyncio_detailed(
            client=client,
            body=body,
        )
    ).parsed
