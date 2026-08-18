from http import HTTPStatus
from typing import Any

import httpx

from ... import errors
from ...client import AuthenticatedClient, Client
from ...models.device_authorization_response import DeviceAuthorizationResponse
from ...models.o_auth_2_error import OAuth2Error
from ...models.post_oauth_2_device_authorization_body import PostOauth2DeviceAuthorizationBody
from ...types import Response


def _get_kwargs(
    *,
    body: PostOauth2DeviceAuthorizationBody,
) -> dict[str, Any]:
    headers: dict[str, Any] = {}

    _kwargs: dict[str, Any] = {
        "method": "post",
        "url": "/oauth2/device_authorization",
    }

    _kwargs["data"] = body.to_dict()
    headers["Content-Type"] = "application/x-www-form-urlencoded"

    _kwargs["headers"] = headers
    return _kwargs


def _parse_response(
    *, client: AuthenticatedClient | Client, response: httpx.Response
) -> DeviceAuthorizationResponse | OAuth2Error | None:
    if response.status_code == 200:
        response_200 = DeviceAuthorizationResponse.from_dict(response.json())

        return response_200

    if response.status_code == 400:
        response_400 = OAuth2Error.from_dict(response.json())

        return response_400

    if response.status_code == 401:
        response_401 = OAuth2Error.from_dict(response.json())

        return response_401

    if response.status_code == 500:
        response_500 = OAuth2Error.from_dict(response.json())

        return response_500

    if client.raise_on_unexpected_status:
        raise errors.UnexpectedStatus(response.status_code, response.content)
    else:
        return None


def _build_response(
    *, client: AuthenticatedClient | Client, response: httpx.Response
) -> Response[DeviceAuthorizationResponse | OAuth2Error]:
    return Response(
        status_code=HTTPStatus(response.status_code),
        content=response.content,
        headers=response.headers,
        parsed=_parse_response(client=client, response=response),
    )


def sync_detailed(
    *,
    client: AuthenticatedClient | Client,
    body: PostOauth2DeviceAuthorizationBody,
) -> Response[DeviceAuthorizationResponse | OAuth2Error]:
    """Device Authorization

     RFC 8628 device authorization endpoint. A CONFIDENTIAL client authenticates with client_secret (HTTP
    Basic preferred); a PUBLIC client sends client_id only. The user then visits verification_uri and an
    admin approves via /oauth2/device/approve; the device polls /oauth2/token with
    grant_type=urn:ietf:params:oauth:grant-type:device_code.

    Args:
        body (PostOauth2DeviceAuthorizationBody):

    Raises:
        errors.UnexpectedStatus: If the server returns an undocumented status code and Client.raise_on_unexpected_status is True.
        httpx.TimeoutException: If the request takes longer than Client.timeout.

    Returns:
        Response[DeviceAuthorizationResponse | OAuth2Error]
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
    body: PostOauth2DeviceAuthorizationBody,
) -> DeviceAuthorizationResponse | OAuth2Error | None:
    """Device Authorization

     RFC 8628 device authorization endpoint. A CONFIDENTIAL client authenticates with client_secret (HTTP
    Basic preferred); a PUBLIC client sends client_id only. The user then visits verification_uri and an
    admin approves via /oauth2/device/approve; the device polls /oauth2/token with
    grant_type=urn:ietf:params:oauth:grant-type:device_code.

    Args:
        body (PostOauth2DeviceAuthorizationBody):

    Raises:
        errors.UnexpectedStatus: If the server returns an undocumented status code and Client.raise_on_unexpected_status is True.
        httpx.TimeoutException: If the request takes longer than Client.timeout.

    Returns:
        DeviceAuthorizationResponse | OAuth2Error
    """

    return sync_detailed(
        client=client,
        body=body,
    ).parsed


async def asyncio_detailed(
    *,
    client: AuthenticatedClient | Client,
    body: PostOauth2DeviceAuthorizationBody,
) -> Response[DeviceAuthorizationResponse | OAuth2Error]:
    """Device Authorization

     RFC 8628 device authorization endpoint. A CONFIDENTIAL client authenticates with client_secret (HTTP
    Basic preferred); a PUBLIC client sends client_id only. The user then visits verification_uri and an
    admin approves via /oauth2/device/approve; the device polls /oauth2/token with
    grant_type=urn:ietf:params:oauth:grant-type:device_code.

    Args:
        body (PostOauth2DeviceAuthorizationBody):

    Raises:
        errors.UnexpectedStatus: If the server returns an undocumented status code and Client.raise_on_unexpected_status is True.
        httpx.TimeoutException: If the request takes longer than Client.timeout.

    Returns:
        Response[DeviceAuthorizationResponse | OAuth2Error]
    """

    kwargs = _get_kwargs(
        body=body,
    )

    response = await client.get_async_httpx_client().request(**kwargs)

    return _build_response(client=client, response=response)


async def asyncio(
    *,
    client: AuthenticatedClient | Client,
    body: PostOauth2DeviceAuthorizationBody,
) -> DeviceAuthorizationResponse | OAuth2Error | None:
    """Device Authorization

     RFC 8628 device authorization endpoint. A CONFIDENTIAL client authenticates with client_secret (HTTP
    Basic preferred); a PUBLIC client sends client_id only. The user then visits verification_uri and an
    admin approves via /oauth2/device/approve; the device polls /oauth2/token with
    grant_type=urn:ietf:params:oauth:grant-type:device_code.

    Args:
        body (PostOauth2DeviceAuthorizationBody):

    Raises:
        errors.UnexpectedStatus: If the server returns an undocumented status code and Client.raise_on_unexpected_status is True.
        httpx.TimeoutException: If the request takes longer than Client.timeout.

    Returns:
        DeviceAuthorizationResponse | OAuth2Error
    """

    return (
        await asyncio_detailed(
            client=client,
            body=body,
        )
    ).parsed
