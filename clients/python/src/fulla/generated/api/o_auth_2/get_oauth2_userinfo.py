from http import HTTPStatus
from typing import Any, cast

import httpx

from ... import errors
from ...client import AuthenticatedClient, Client
from ...models.o_auth_2_error import OAuth2Error
from ...models.user_info_response import UserInfoResponse
from ...types import Response


def _get_kwargs() -> dict[str, Any]:

    _kwargs: dict[str, Any] = {
        "method": "get",
        "url": "/oauth2/userinfo",
    }

    return _kwargs


def _parse_response(
    *, client: AuthenticatedClient | Client, response: httpx.Response
) -> Any | OAuth2Error | UserInfoResponse | None:
    if response.status_code == 200:
        response_200 = UserInfoResponse.from_dict(response.json())

        return response_200

    if response.status_code == 400:
        response_400 = cast(Any, None)
        return response_400

    if response.status_code == 401:
        response_401 = OAuth2Error.from_dict(response.json())

        return response_401

    if response.status_code == 403:
        response_403 = OAuth2Error.from_dict(response.json())

        return response_403

    if response.status_code == 404:
        response_404 = cast(Any, None)
        return response_404

    if client.raise_on_unexpected_status:
        raise errors.UnexpectedStatus(response.status_code, response.content)
    else:
        return None


def _build_response(
    *, client: AuthenticatedClient | Client, response: httpx.Response
) -> Response[Any | OAuth2Error | UserInfoResponse]:
    return Response(
        status_code=HTTPStatus(response.status_code),
        content=response.content,
        headers=response.headers,
        parsed=_parse_response(client=client, response=response),
    )


def sync_detailed(
    *,
    client: AuthenticatedClient,
) -> Response[Any | OAuth2Error | UserInfoResponse]:
    """Get user information

     Returns information about the authenticated user. F-023 (OIDC Core §5.3): the access token's scope
    MUST include openid; M2M tokens (client_credentials, subject client:*) are rejected. The response
    includes the email_verified claim (F-024) when an email is present.

    Raises:
        errors.UnexpectedStatus: If the server returns an undocumented status code and Client.raise_on_unexpected_status is True.
        httpx.TimeoutException: If the request takes longer than Client.timeout.

    Returns:
        Response[Any | OAuth2Error | UserInfoResponse]
    """

    kwargs = _get_kwargs()

    response = client.get_httpx_client().request(
        **kwargs,
    )

    return _build_response(client=client, response=response)


def sync(
    *,
    client: AuthenticatedClient,
) -> Any | OAuth2Error | UserInfoResponse | None:
    """Get user information

     Returns information about the authenticated user. F-023 (OIDC Core §5.3): the access token's scope
    MUST include openid; M2M tokens (client_credentials, subject client:*) are rejected. The response
    includes the email_verified claim (F-024) when an email is present.

    Raises:
        errors.UnexpectedStatus: If the server returns an undocumented status code and Client.raise_on_unexpected_status is True.
        httpx.TimeoutException: If the request takes longer than Client.timeout.

    Returns:
        Any | OAuth2Error | UserInfoResponse
    """

    return sync_detailed(
        client=client,
    ).parsed


async def asyncio_detailed(
    *,
    client: AuthenticatedClient,
) -> Response[Any | OAuth2Error | UserInfoResponse]:
    """Get user information

     Returns information about the authenticated user. F-023 (OIDC Core §5.3): the access token's scope
    MUST include openid; M2M tokens (client_credentials, subject client:*) are rejected. The response
    includes the email_verified claim (F-024) when an email is present.

    Raises:
        errors.UnexpectedStatus: If the server returns an undocumented status code and Client.raise_on_unexpected_status is True.
        httpx.TimeoutException: If the request takes longer than Client.timeout.

    Returns:
        Response[Any | OAuth2Error | UserInfoResponse]
    """

    kwargs = _get_kwargs()

    response = await client.get_async_httpx_client().request(**kwargs)

    return _build_response(client=client, response=response)


async def asyncio(
    *,
    client: AuthenticatedClient,
) -> Any | OAuth2Error | UserInfoResponse | None:
    """Get user information

     Returns information about the authenticated user. F-023 (OIDC Core §5.3): the access token's scope
    MUST include openid; M2M tokens (client_credentials, subject client:*) are rejected. The response
    includes the email_verified claim (F-024) when an email is present.

    Raises:
        errors.UnexpectedStatus: If the server returns an undocumented status code and Client.raise_on_unexpected_status is True.
        httpx.TimeoutException: If the request takes longer than Client.timeout.

    Returns:
        Any | OAuth2Error | UserInfoResponse
    """

    return (
        await asyncio_detailed(
            client=client,
        )
    ).parsed
