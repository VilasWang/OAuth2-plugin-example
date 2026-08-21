from http import HTTPStatus
from typing import Any
from urllib.parse import quote

import httpx

from ... import errors
from ...client import AuthenticatedClient, Client
from ...models.error_envelope import ErrorEnvelope
from ...models.post_api_me_social_links_provider_body import PostApiMeSocialLinksProviderBody
from ...models.post_api_me_social_links_provider_provider import PostApiMeSocialLinksProviderProvider
from ...models.social_link_result import SocialLinkResult
from ...types import Response


def _get_kwargs(
    provider: PostApiMeSocialLinksProviderProvider,
    *,
    body: PostApiMeSocialLinksProviderBody,
) -> dict[str, Any]:
    headers: dict[str, Any] = {}

    _kwargs: dict[str, Any] = {
        "method": "post",
        "url": "/api/me/social/links/{provider}".format(
            provider=quote(str(provider), safe=""),
        ),
    }

    _kwargs["json"] = body.to_dict()

    headers["Content-Type"] = "application/json"

    _kwargs["headers"] = headers
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

    if response.status_code == 502:
        response_502 = ErrorEnvelope.from_dict(response.json())

        return response_502

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
    provider: PostApiMeSocialLinksProviderProvider,
    *,
    client: AuthenticatedClient,
    body: PostApiMeSocialLinksProviderBody,
) -> Response[ErrorEnvelope | SocialLinkResult]:
    """Link Social Account

     Verify a provider authorization code (the SPA completes the provider redirect and submits the code)
    and link that provider identity to the current user. Conflicts (409) when the identity is already
    linked to any account or the user already has a different mapping for the provider. Requires the
    `profile` scope.

    Args:
        provider (PostApiMeSocialLinksProviderProvider):
        body (PostApiMeSocialLinksProviderBody):

    Raises:
        errors.UnexpectedStatus: If the server returns an undocumented status code and Client.raise_on_unexpected_status is True.
        httpx.TimeoutException: If the request takes longer than Client.timeout.

    Returns:
        Response[ErrorEnvelope | SocialLinkResult]
    """

    kwargs = _get_kwargs(
        provider=provider,
        body=body,
    )

    response = client.get_httpx_client().request(
        **kwargs,
    )

    return _build_response(client=client, response=response)


def sync(
    provider: PostApiMeSocialLinksProviderProvider,
    *,
    client: AuthenticatedClient,
    body: PostApiMeSocialLinksProviderBody,
) -> ErrorEnvelope | SocialLinkResult | None:
    """Link Social Account

     Verify a provider authorization code (the SPA completes the provider redirect and submits the code)
    and link that provider identity to the current user. Conflicts (409) when the identity is already
    linked to any account or the user already has a different mapping for the provider. Requires the
    `profile` scope.

    Args:
        provider (PostApiMeSocialLinksProviderProvider):
        body (PostApiMeSocialLinksProviderBody):

    Raises:
        errors.UnexpectedStatus: If the server returns an undocumented status code and Client.raise_on_unexpected_status is True.
        httpx.TimeoutException: If the request takes longer than Client.timeout.

    Returns:
        ErrorEnvelope | SocialLinkResult
    """

    return sync_detailed(
        provider=provider,
        client=client,
        body=body,
    ).parsed


async def asyncio_detailed(
    provider: PostApiMeSocialLinksProviderProvider,
    *,
    client: AuthenticatedClient,
    body: PostApiMeSocialLinksProviderBody,
) -> Response[ErrorEnvelope | SocialLinkResult]:
    """Link Social Account

     Verify a provider authorization code (the SPA completes the provider redirect and submits the code)
    and link that provider identity to the current user. Conflicts (409) when the identity is already
    linked to any account or the user already has a different mapping for the provider. Requires the
    `profile` scope.

    Args:
        provider (PostApiMeSocialLinksProviderProvider):
        body (PostApiMeSocialLinksProviderBody):

    Raises:
        errors.UnexpectedStatus: If the server returns an undocumented status code and Client.raise_on_unexpected_status is True.
        httpx.TimeoutException: If the request takes longer than Client.timeout.

    Returns:
        Response[ErrorEnvelope | SocialLinkResult]
    """

    kwargs = _get_kwargs(
        provider=provider,
        body=body,
    )

    response = await client.get_async_httpx_client().request(**kwargs)

    return _build_response(client=client, response=response)


async def asyncio(
    provider: PostApiMeSocialLinksProviderProvider,
    *,
    client: AuthenticatedClient,
    body: PostApiMeSocialLinksProviderBody,
) -> ErrorEnvelope | SocialLinkResult | None:
    """Link Social Account

     Verify a provider authorization code (the SPA completes the provider redirect and submits the code)
    and link that provider identity to the current user. Conflicts (409) when the identity is already
    linked to any account or the user already has a different mapping for the provider. Requires the
    `profile` scope.

    Args:
        provider (PostApiMeSocialLinksProviderProvider):
        body (PostApiMeSocialLinksProviderBody):

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
            body=body,
        )
    ).parsed
