from http import HTTPStatus
from typing import Any

import httpx

from ... import errors
from ...client import AuthenticatedClient, Client
from ...models.error_envelope import ErrorEnvelope
from ...models.mfa_verify_request import MfaVerifyRequest
from ...models.post_oauth_2_mfa_verify_response_200 import PostOauth2MfaVerifyResponse200
from ...types import UNSET, Response


def _get_kwargs(
    *,
    body: MfaVerifyRequest | MfaVerifyRequest | Unset = UNSET,
) -> dict[str, Any]:
    headers: dict[str, Any] = {}

    _kwargs: dict[str, Any] = {
        "method": "post",
        "url": "/oauth2/mfa/verify",
    }

    if isinstance(body, MfaVerifyRequest):
        _kwargs["json"] = body.to_dict()

        headers["Content-Type"] = "application/json"
    if isinstance(body, MfaVerifyRequest):
        _kwargs["data"] = body.to_dict()
        headers["Content-Type"] = "application/x-www-form-urlencoded"

    _kwargs["headers"] = headers
    return _kwargs


def _parse_response(
    *, client: AuthenticatedClient | Client, response: httpx.Response
) -> ErrorEnvelope | PostOauth2MfaVerifyResponse200 | None:
    if response.status_code == 200:
        response_200 = PostOauth2MfaVerifyResponse200.from_dict(response.json())

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
) -> Response[ErrorEnvelope | PostOauth2MfaVerifyResponse200]:
    return Response(
        status_code=HTTPStatus(response.status_code),
        content=response.content,
        headers=response.headers,
        parsed=_parse_response(client=client, response=response),
    )


def sync_detailed(
    *,
    client: AuthenticatedClient | Client,
    body: MfaVerifyRequest | MfaVerifyRequest | Unset = UNSET,
) -> Response[ErrorEnvelope | PostOauth2MfaVerifyResponse200]:
    """Verify MFA Code (Login)

     Verify MFA (TOTP) code during login. Completes the authorization-code issuance started by
    /oauth2/login when MFA was required. The PKCE code_verifier (C4, RFC 7636) must be supplied so the
    internally-generated code passes PKCE verification against the code_challenge persisted on the
    session during the first-factor login step. Accepts a JSON body or form-encoded parameters.

    Args:
        body (MfaVerifyRequest): /oauth2/mfa/verify completion payload (JSON body or form-
            encoded).
        body (MfaVerifyRequest): /oauth2/mfa/verify completion payload (JSON body or form-
            encoded).

    Raises:
        errors.UnexpectedStatus: If the server returns an undocumented status code and Client.raise_on_unexpected_status is True.
        httpx.TimeoutException: If the request takes longer than Client.timeout.

    Returns:
        Response[ErrorEnvelope | PostOauth2MfaVerifyResponse200]
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
    body: MfaVerifyRequest | MfaVerifyRequest | Unset = UNSET,
) -> ErrorEnvelope | PostOauth2MfaVerifyResponse200 | None:
    """Verify MFA Code (Login)

     Verify MFA (TOTP) code during login. Completes the authorization-code issuance started by
    /oauth2/login when MFA was required. The PKCE code_verifier (C4, RFC 7636) must be supplied so the
    internally-generated code passes PKCE verification against the code_challenge persisted on the
    session during the first-factor login step. Accepts a JSON body or form-encoded parameters.

    Args:
        body (MfaVerifyRequest): /oauth2/mfa/verify completion payload (JSON body or form-
            encoded).
        body (MfaVerifyRequest): /oauth2/mfa/verify completion payload (JSON body or form-
            encoded).

    Raises:
        errors.UnexpectedStatus: If the server returns an undocumented status code and Client.raise_on_unexpected_status is True.
        httpx.TimeoutException: If the request takes longer than Client.timeout.

    Returns:
        ErrorEnvelope | PostOauth2MfaVerifyResponse200
    """

    return sync_detailed(
        client=client,
        body=body,
    ).parsed


async def asyncio_detailed(
    *,
    client: AuthenticatedClient | Client,
    body: MfaVerifyRequest | MfaVerifyRequest | Unset = UNSET,
) -> Response[ErrorEnvelope | PostOauth2MfaVerifyResponse200]:
    """Verify MFA Code (Login)

     Verify MFA (TOTP) code during login. Completes the authorization-code issuance started by
    /oauth2/login when MFA was required. The PKCE code_verifier (C4, RFC 7636) must be supplied so the
    internally-generated code passes PKCE verification against the code_challenge persisted on the
    session during the first-factor login step. Accepts a JSON body or form-encoded parameters.

    Args:
        body (MfaVerifyRequest): /oauth2/mfa/verify completion payload (JSON body or form-
            encoded).
        body (MfaVerifyRequest): /oauth2/mfa/verify completion payload (JSON body or form-
            encoded).

    Raises:
        errors.UnexpectedStatus: If the server returns an undocumented status code and Client.raise_on_unexpected_status is True.
        httpx.TimeoutException: If the request takes longer than Client.timeout.

    Returns:
        Response[ErrorEnvelope | PostOauth2MfaVerifyResponse200]
    """

    kwargs = _get_kwargs(
        body=body,
    )

    response = await client.get_async_httpx_client().request(**kwargs)

    return _build_response(client=client, response=response)


async def asyncio(
    *,
    client: AuthenticatedClient | Client,
    body: MfaVerifyRequest | MfaVerifyRequest | Unset = UNSET,
) -> ErrorEnvelope | PostOauth2MfaVerifyResponse200 | None:
    """Verify MFA Code (Login)

     Verify MFA (TOTP) code during login. Completes the authorization-code issuance started by
    /oauth2/login when MFA was required. The PKCE code_verifier (C4, RFC 7636) must be supplied so the
    internally-generated code passes PKCE verification against the code_challenge persisted on the
    session during the first-factor login step. Accepts a JSON body or form-encoded parameters.

    Args:
        body (MfaVerifyRequest): /oauth2/mfa/verify completion payload (JSON body or form-
            encoded).
        body (MfaVerifyRequest): /oauth2/mfa/verify completion payload (JSON body or form-
            encoded).

    Raises:
        errors.UnexpectedStatus: If the server returns an undocumented status code and Client.raise_on_unexpected_status is True.
        httpx.TimeoutException: If the request takes longer than Client.timeout.

    Returns:
        ErrorEnvelope | PostOauth2MfaVerifyResponse200
    """

    return (
        await asyncio_detailed(
            client=client,
            body=body,
        )
    ).parsed
