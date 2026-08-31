from http import HTTPStatus
from typing import Any

import httpx

from ... import errors
from ...client import AuthenticatedClient, Client
from ...models.web_authn_assertion_credential import WebAuthnAssertionCredential
from ...types import Response


def _get_kwargs(
    *,
    body: WebAuthnAssertionCredential,
) -> dict[str, Any]:
    headers: dict[str, Any] = {}

    _kwargs: dict[str, Any] = {
        "method": "post",
        "url": "/oauth2/webauthn/authenticate/finish",
    }

    _kwargs["json"] = body.to_dict()

    headers["Content-Type"] = "application/json"

    _kwargs["headers"] = headers
    return _kwargs


def _parse_response(*, client: AuthenticatedClient | Client, response: httpx.Response) -> Any | None:
    if response.status_code == 200:
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
    client: AuthenticatedClient | Client,
    body: WebAuthnAssertionCredential,
) -> Response[Any]:
    """WebAuthn Authenticate Finish

     Finish passkey authentication (#142): the session challenge is consumed unconditionally, the ES256
    signature over authData || SHA256(clientDataJSON) is verified against the STORED COSE key, UV=1 is
    enforced and signCount regression is treated as cloning. On success a browser session is established
    (Set-Cookie). All failures answer the generic AUTH_INVALID_CREDENTIALS.

    Args:
        body (WebAuthnAssertionCredential): Browser PublicKeyCredential for
            navigator.credentials.get() (#142). All binary fields are base64url without padding.

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
    client: AuthenticatedClient | Client,
    body: WebAuthnAssertionCredential,
) -> Response[Any]:
    """WebAuthn Authenticate Finish

     Finish passkey authentication (#142): the session challenge is consumed unconditionally, the ES256
    signature over authData || SHA256(clientDataJSON) is verified against the STORED COSE key, UV=1 is
    enforced and signCount regression is treated as cloning. On success a browser session is established
    (Set-Cookie). All failures answer the generic AUTH_INVALID_CREDENTIALS.

    Args:
        body (WebAuthnAssertionCredential): Browser PublicKeyCredential for
            navigator.credentials.get() (#142). All binary fields are base64url without padding.

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
