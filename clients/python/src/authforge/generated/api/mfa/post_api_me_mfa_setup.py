from http import HTTPStatus
from typing import Any

import httpx

from ... import errors
from ...client import AuthenticatedClient, Client
from ...models.error_envelope import ErrorEnvelope
from ...models.post_api_me_mfa_setup_body import PostApiMeMfaSetupBody
from ...models.post_api_me_mfa_setup_response_200 import PostApiMeMfaSetupResponse200
from ...types import UNSET, Response, Unset


def _get_kwargs(
    *,
    body: PostApiMeMfaSetupBody | Unset = UNSET,
) -> dict[str, Any]:
    headers: dict[str, Any] = {}

    _kwargs: dict[str, Any] = {
        "method": "post",
        "url": "/api/me/mfa/setup",
    }

    if not isinstance(body, Unset):
        _kwargs["json"] = body.to_dict()

    headers["Content-Type"] = "application/json"

    _kwargs["headers"] = headers
    return _kwargs


def _parse_response(
    *, client: AuthenticatedClient | Client, response: httpx.Response
) -> ErrorEnvelope | PostApiMeMfaSetupResponse200 | None:
    if response.status_code == 200:
        response_200 = PostApiMeMfaSetupResponse200.from_dict(response.json())

        return response_200

    if response.status_code == 401:
        response_401 = ErrorEnvelope.from_dict(response.json())

        return response_401

    if client.raise_on_unexpected_status:
        raise errors.UnexpectedStatus(response.status_code, response.content)
    else:
        return None


def _build_response(
    *, client: AuthenticatedClient | Client, response: httpx.Response
) -> Response[ErrorEnvelope | PostApiMeMfaSetupResponse200]:
    return Response(
        status_code=HTTPStatus(response.status_code),
        content=response.content,
        headers=response.headers,
        parsed=_parse_response(client=client, response=response),
    )


def sync_detailed(
    *,
    client: AuthenticatedClient,
    body: PostApiMeMfaSetupBody | Unset = UNSET,
) -> Response[ErrorEnvelope | PostApiMeMfaSetupResponse200]:
    """Setup MFA

     Initiate MFA setup by generating a TOTP secret. Returns the base32 secret and an otpauth:// URI for
    authenticator apps / QR codes.

    Args:
        body (PostApiMeMfaSetupBody | Unset):

    Raises:
        errors.UnexpectedStatus: If the server returns an undocumented status code and Client.raise_on_unexpected_status is True.
        httpx.TimeoutException: If the request takes longer than Client.timeout.

    Returns:
        Response[ErrorEnvelope | PostApiMeMfaSetupResponse200]
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
    body: PostApiMeMfaSetupBody | Unset = UNSET,
) -> ErrorEnvelope | PostApiMeMfaSetupResponse200 | None:
    """Setup MFA

     Initiate MFA setup by generating a TOTP secret. Returns the base32 secret and an otpauth:// URI for
    authenticator apps / QR codes.

    Args:
        body (PostApiMeMfaSetupBody | Unset):

    Raises:
        errors.UnexpectedStatus: If the server returns an undocumented status code and Client.raise_on_unexpected_status is True.
        httpx.TimeoutException: If the request takes longer than Client.timeout.

    Returns:
        ErrorEnvelope | PostApiMeMfaSetupResponse200
    """

    return sync_detailed(
        client=client,
        body=body,
    ).parsed


async def asyncio_detailed(
    *,
    client: AuthenticatedClient,
    body: PostApiMeMfaSetupBody | Unset = UNSET,
) -> Response[ErrorEnvelope | PostApiMeMfaSetupResponse200]:
    """Setup MFA

     Initiate MFA setup by generating a TOTP secret. Returns the base32 secret and an otpauth:// URI for
    authenticator apps / QR codes.

    Args:
        body (PostApiMeMfaSetupBody | Unset):

    Raises:
        errors.UnexpectedStatus: If the server returns an undocumented status code and Client.raise_on_unexpected_status is True.
        httpx.TimeoutException: If the request takes longer than Client.timeout.

    Returns:
        Response[ErrorEnvelope | PostApiMeMfaSetupResponse200]
    """

    kwargs = _get_kwargs(
        body=body,
    )

    response = await client.get_async_httpx_client().request(**kwargs)

    return _build_response(client=client, response=response)


async def asyncio(
    *,
    client: AuthenticatedClient,
    body: PostApiMeMfaSetupBody | Unset = UNSET,
) -> ErrorEnvelope | PostApiMeMfaSetupResponse200 | None:
    """Setup MFA

     Initiate MFA setup by generating a TOTP secret. Returns the base32 secret and an otpauth:// URI for
    authenticator apps / QR codes.

    Args:
        body (PostApiMeMfaSetupBody | Unset):

    Raises:
        errors.UnexpectedStatus: If the server returns an undocumented status code and Client.raise_on_unexpected_status is True.
        httpx.TimeoutException: If the request takes longer than Client.timeout.

    Returns:
        ErrorEnvelope | PostApiMeMfaSetupResponse200
    """

    return (
        await asyncio_detailed(
            client=client,
            body=body,
        )
    ).parsed
