from http import HTTPStatus
from typing import Any
from urllib.parse import quote

import httpx

from ... import errors
from ...client import AuthenticatedClient, Client
from ...models.delete_api_me_social_links_provider_provider import DeleteApiMeSocialLinksProviderProvider
from ...models.error_envelope import ErrorEnvelope
from ...models.social_link_result import SocialLinkResult
from ...types import Response


def _get_kwargs(
    provider: DeleteApiMeSocialLinksProviderProvider,
) -> dict[str, Any]:

    _kwargs: dict[str, Any] = {
        "method": "delete",
        "url": "/api/me/social/links/{provider}".format(
            provider=quote(str(provider), safe=""),
        ),
    }

    return _kwargs


def _parse_response(
    *, client: AuthenticatedClient | Client, response: httpx.Response
) -> ErrorEnvelope | SocialLinkResult | None:
    if response.status_code == 200:
        response_200 = SocialLinkResult.from_dict(response.json())

        return response_200

    if response.status_code == 400:
        response_400 = ErrorEnvelope.from_dict(response.json())

        return response_400

    if response.status_code == 401:
        response_401 = ErrorEnvelope.from_dict(response.json())

        return response_401

    if response.status_code == 404:
        response_404 = ErrorEnvelope.from_dict(response.json())

        return response_404

    if response.status_code == 409:
        response_409 = ErrorEnvelope.from_dict(response.json())

        return response_409

    if response.status_code == 500:
        response_500 = ErrorEnvelope.from_dict(response.json())

        return response_500

    if client.raise_on_unexpected_status:
        raise errors.UnexpectedStatus(response.status_code, response.content)
    else:
        return None


def _build_response(
    *, client: AuthenticatedClient | Client, response: httpx.Response
) -> Response[ErrorEnvelope | SocialLinkResult]:
    return Response(
        status_code=HTTPStatus(response.status_code),
        content=response.content,
        headers=response.headers,
        parsed=_parse_response(client=client, response=response),
    )


def sync_detailed(
    provider: DeleteApiMeSocialLinksProviderProvider,
    *,
    client: AuthenticatedClient,
) -> Response[ErrorEnvelope | SocialLinkResult]:
    """Unlink Social Account

     Remove the current user's linked identity for a provider. Existing sessions issued through that
    provider are NOT revoked -- they stay valid until natural expiry. The provider identity cannot be
    used for new sign-ins until it is linked to an account again. Refused with 409 when it is the user's
    last social link and the user has no usable password (last-credential guard). Requires the `profile`
    scope.

    Args:
        provider (DeleteApiMeSocialLinksProviderProvider):

    Raises:
        errors.UnexpectedStatus: If the server returns an undocumented status code and Client.raise_on_unexpected_status is True.
        httpx.TimeoutException: If the request takes longer than Client.timeout.

    Returns:
        Response[ErrorEnvelope | SocialLinkResult]
    """

    kwargs = _get_kwargs(
        provider=provider,
    )

    response = client.get_httpx_client().request(
        **kwargs,
    )

    return _build_response(client=client, response=response)


def sync(
    provider: DeleteApiMeSocialLinksProviderProvider,
    *,
    client: AuthenticatedClient,
) -> ErrorEnvelope | SocialLinkResult | None:
    """Unlink Social Account

     Remove the current user's linked identity for a provider. Existing sessions issued through that
    provider are NOT revoked -- they stay valid until natural expiry. The provider identity cannot be
    used for new sign-ins until it is linked to an account again. Refused with 409 when it is the user's
    last social link and the user has no usable password (last-credential guard). Requires the `profile`
    scope.

    Args:
        provider (DeleteApiMeSocialLinksProviderProvider):

    Raises:
        errors.UnexpectedStatus: If the server returns an undocumented status code and Client.raise_on_unexpected_status is True.
        httpx.TimeoutException: If the request takes longer than Client.timeout.

    Returns:
        ErrorEnvelope | SocialLinkResult
    """

    return sync_detailed(
        provider=provider,
        client=client,
    ).parsed


async def asyncio_detailed(
    provider: DeleteApiMeSocialLinksProviderProvider,
    *,
    client: AuthenticatedClient,
) -> Response[ErrorEnvelope | SocialLinkResult]:
    """Unlink Social Account

     Remove the current user's linked identity for a provider. Existing sessions issued through that
    provider are NOT revoked -- they stay valid until natural expiry. The provider identity cannot be
    used for new sign-ins until it is linked to an account again. Refused with 409 when it is the user's
    last social link and the user has no usable password (last-credential guard). Requires the `profile`
    scope.

    Args:
        provider (DeleteApiMeSocialLinksProviderProvider):

    Raises:
        errors.UnexpectedStatus: If the server returns an undocumented status code and Client.raise_on_unexpected_status is True.
        httpx.TimeoutException: If the request takes longer than Client.timeout.

    Returns:
        Response[ErrorEnvelope | SocialLinkResult]
    """

    kwargs = _get_kwargs(
        provider=provider,
    )

    response = await client.get_async_httpx_client().request(**kwargs)

    return _build_response(client=client, response=response)


async def asyncio(
    provider: DeleteApiMeSocialLinksProviderProvider,
    *,
    client: AuthenticatedClient,
) -> ErrorEnvelope | SocialLinkResult | None:
    """Unlink Social Account

     Remove the current user's linked identity for a provider. Existing sessions issued through that
    provider are NOT revoked -- they stay valid until natural expiry. The provider identity cannot be
    used for new sign-ins until it is linked to an account again. Refused with 409 when it is the user's
    last social link and the user has no usable password (last-credential guard). Requires the `profile`
    scope.

    Args:
        provider (DeleteApiMeSocialLinksProviderProvider):

    Raises:
        errors.UnexpectedStatus: If the server returns an undocumented status code and Client.raise_on_unexpected_status is True.
        httpx.TimeoutException: If the request takes longer than Client.timeout.

    Returns:
        ErrorEnvelope | SocialLinkResult
    """

    return (
        await asyncio_detailed(
            provider=provider,
            client=client,
        )
    ).parsed
