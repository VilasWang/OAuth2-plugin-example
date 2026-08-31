from http import HTTPStatus
from typing import Any

import httpx

from ... import errors
from ...client import AuthenticatedClient, Client
from ...models.web_authn_registration_credential import WebAuthnRegistrationCredential
from ...types import Response


def _get_kwargs(
    *,
    body: WebAuthnRegistrationCredential,
) -> dict[str, Any]:
    headers: dict[str, Any] = {}

    _kwargs: dict[str, Any] = {
        "method": "post",
        "url": "/api/me/webauthn/register/finish",
    }

    _kwargs["json"] = body.to_dict()

    headers["Content-Type"] = "application/json"

    _kwargs["headers"] = headers
    return _kwargs


def _parse_response(*, client: AuthenticatedClient | Client, response: httpx.Response) -> Any | None:
    if response.status_code == 201:
        return None

    if response.status_code == 400:
        return None

    if response.status_code == 401:
        return None

    if client.raise_on_unexpected_status:
        raise errors.UnexpectedStatus(response.status_code, response.content)
    else:
        return None


def _build_response(*, client: AuthenticatedClient | Client, response: httpx.Response) -> Response[Any]:
    return Response(
        status_code=HTTPStatus(response.status_code),
        content=response.content,
        headers=response.headers,
        parsed=_parse_response(client=client, response=response),
    )


def sync_detailed(
    *,
    client: AuthenticatedClient,
    body: WebAuthnRegistrationCredential,
) -> Response[Any]:
    """WebAuthn Register Finish

     Finish passkey registration (#142): the browser PublicKeyCredential is verified server-side
    (subject-bound challenge, origin allowlist, none-format attestation, ES256 COSE key) before anything
    is stored; the legacy {credential_id, public_key} body is rejected.

    Args:
        body (WebAuthnRegistrationCredential): Browser PublicKeyCredential for
            navigator.credentials.create() (#142). All binary fields are base64url without padding.
            The server ignores any client-submitted public_key: the stored key comes from the verified
            attestation object's COSE bytes.

    Raises:
        errors.UnexpectedStatus: If the server returns an undocumented status code and Client.raise_on_unexpected_status is True.
        httpx.TimeoutException: If the request takes longer than Client.timeout.

    Returns:
        Response[Any]
    """

    kwargs = _get_kwargs(
        body=body,
    )

    response = client.get_httpx_client().request(
        **kwargs,
    )

    return _build_response(client=client, response=response)


async def asyncio_detailed(
    *,
    client: AuthenticatedClient,
    body: WebAuthnRegistrationCredential,
) -> Response[Any]:
    """WebAuthn Register Finish

     Finish passkey registration (#142): the browser PublicKeyCredential is verified server-side
    (subject-bound challenge, origin allowlist, none-format attestation, ES256 COSE key) before anything
    is stored; the legacy {credential_id, public_key} body is rejected.

    Args:
        body (WebAuthnRegistrationCredential): Browser PublicKeyCredential for
            navigator.credentials.create() (#142). All binary fields are base64url without padding.
            The server ignores any client-submitted public_key: the stored key comes from the verified
            attestation object's COSE bytes.

    Raises:
        errors.UnexpectedStatus: If the server returns an undocumented status code and Client.raise_on_unexpected_status is True.
        httpx.TimeoutException: If the request takes longer than Client.timeout.

    Returns:
        Response[Any]
    """

    kwargs = _get_kwargs(
        body=body,
    )

    response = await client.get_async_httpx_client().request(**kwargs)

    return _build_response(client=client, response=response)
