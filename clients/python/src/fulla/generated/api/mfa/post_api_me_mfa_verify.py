from http import HTTPStatus
from typing import Any

import httpx

from ... import errors
from ...client import AuthenticatedClient, Client
from ...models.error_envelope import ErrorEnvelope
from ...models.post_api_me_mfa_verify_data_body import PostApiMeMfaVerifyDataBody
from ...models.post_api_me_mfa_verify_json_body import PostApiMeMfaVerifyJsonBody
from ...models.post_api_me_mfa_verify_response_200 import PostApiMeMfaVerifyResponse200
from ...types import UNSET, Response


def _get_kwargs(
    *,
    body: PostApiMeMfaVerifyJsonBody | PostApiMeMfaVerifyDataBody | Unset = UNSET,
) -> dict[str, Any]:
    headers: dict[str, Any] = {}

    _kwargs: dict[str, Any] = {
        "method": "post",
        "url": "/api/me/mfa/verify",
    }

    if isinstance(body, PostApiMeMfaVerifyJsonBody):
        _kwargs["json"] = body.to_dict()

        headers["Content-Type"] = "application/json"
    if isinstance(body, PostApiMeMfaVerifyDataBody):
        _kwargs["data"] = body.to_dict()
        headers["Content-Type"] = "application/x-www-form-urlencoded"

    _kwargs["headers"] = headers
    return _kwargs


def _parse_response(
    *, client: AuthenticatedClient | Client, response: httpx.Response
) -> ErrorEnvelope | PostApiMeMfaVerifyResponse200 | None:
    if response.status_code == 200:
        response_200 = PostApiMeMfaVerifyResponse200.from_dict(response.json())

        return response_200

    if response.status_code == 400:
        response_400 = ErrorEnvelope.from_dict(response.json())

        return response_400

    if response.status_code == 401:
        response_401 = ErrorEnvelope.from_dict(response.json())

        return response_401

    if client.raise_on_unexpected_status:
        raise errors.UnexpectedStatus(response.status_code, response.content)
    else:
        return None


def _build_response(
    *, client: AuthenticatedClient | Client, response: httpx.Response
) -> Response[ErrorEnvelope | PostApiMeMfaVerifyResponse200]:
    return Response(
        status_code=HTTPStatus(response.status_code),
        content=response.content,
        headers=response.headers,
        parsed=_parse_response(client=client, response=response),
    )


def sync_detailed(
    *,
    client: AuthenticatedClient,
    body: PostApiMeMfaVerifyJsonBody | PostApiMeMfaVerifyDataBody | Unset = UNSET,
) -> Response[ErrorEnvelope | PostApiMeMfaVerifyResponse200]:
    """Verify MFA Setup

     Verify a 6-digit TOTP code to finalize MFA setup. Returns 10 single-use backup codes on success.

    Args:
        body (PostApiMeMfaVerifyJsonBody):
        body (PostApiMeMfaVerifyDataBody):

    Raises:
        errors.UnexpectedStatus: If the server returns an undocumented status code and Client.raise_on_unexpected_status is True.
        httpx.TimeoutException: If the request takes longer than Client.timeout.

    Returns:
        Response[ErrorEnvelope | PostApiMeMfaVerifyResponse200]
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
    body: PostApiMeMfaVerifyJsonBody | PostApiMeMfaVerifyDataBody | Unset = UNSET,
) -> ErrorEnvelope | PostApiMeMfaVerifyResponse200 | None:
    """Verify MFA Setup

     Verify a 6-digit TOTP code to finalize MFA setup. Returns 10 single-use backup codes on success.

    Args:
        body (PostApiMeMfaVerifyJsonBody):
        body (PostApiMeMfaVerifyDataBody):

    Raises:
        errors.UnexpectedStatus: If the server returns an undocumented status code and Client.raise_on_unexpected_status is True.
        httpx.TimeoutException: If the request takes longer than Client.timeout.

    Returns:
        ErrorEnvelope | PostApiMeMfaVerifyResponse200
    """

    return sync_detailed(
        client=client,
        body=body,
    ).parsed


async def asyncio_detailed(
    *,
    client: AuthenticatedClient,
    body: PostApiMeMfaVerifyJsonBody | PostApiMeMfaVerifyDataBody | Unset = UNSET,
) -> Response[ErrorEnvelope | PostApiMeMfaVerifyResponse200]:
    """Verify MFA Setup

     Verify a 6-digit TOTP code to finalize MFA setup. Returns 10 single-use backup codes on success.

    Args:
        body (PostApiMeMfaVerifyJsonBody):
        body (PostApiMeMfaVerifyDataBody):

    Raises:
        errors.UnexpectedStatus: If the server returns an undocumented status code and Client.raise_on_unexpected_status is True.
        httpx.TimeoutException: If the request takes longer than Client.timeout.

    Returns:
        Response[ErrorEnvelope | PostApiMeMfaVerifyResponse200]
    """

    kwargs = _get_kwargs(
        body=body,
    )

    response = await client.get_async_httpx_client().request(**kwargs)

    return _build_response(client=client, response=response)


async def asyncio(
    *,
    client: AuthenticatedClient,
    body: PostApiMeMfaVerifyJsonBody | PostApiMeMfaVerifyDataBody | Unset = UNSET,
) -> ErrorEnvelope | PostApiMeMfaVerifyResponse200 | None:
    """Verify MFA Setup

     Verify a 6-digit TOTP code to finalize MFA setup. Returns 10 single-use backup codes on success.

    Args:
        body (PostApiMeMfaVerifyJsonBody):
        body (PostApiMeMfaVerifyDataBody):

    Raises:
        errors.UnexpectedStatus: If the server returns an undocumented status code and Client.raise_on_unexpected_status is True.
        httpx.TimeoutException: If the request takes longer than Client.timeout.

    Returns:
        ErrorEnvelope | PostApiMeMfaVerifyResponse200
    """

    return (
        await asyncio_detailed(
            client=client,
            body=body,
        )
    ).parsed
