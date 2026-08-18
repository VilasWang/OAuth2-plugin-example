from http import HTTPStatus
from typing import Any

import httpx

from ... import errors
from ...client import AuthenticatedClient, Client
from ...models.get_api_admin_scopes_resources_response_200 import GetApiAdminScopesResourcesResponse200
from ...types import Response


def _get_kwargs() -> dict[str, Any]:

    _kwargs: dict[str, Any] = {
        "method": "get",
        "url": "/api/admin/scopes/resources",
    }

    return _kwargs


def _parse_response(
    *, client: AuthenticatedClient | Client, response: httpx.Response
) -> GetApiAdminScopesResourcesResponse200 | None:
    if response.status_code == 200:
        response_200 = GetApiAdminScopesResourcesResponse200.from_dict(response.json())

        return response_200

    if client.raise_on_unexpected_status:
        raise errors.UnexpectedStatus(response.status_code, response.content)
    else:
        return None


def _build_response(
    *, client: AuthenticatedClient | Client, response: httpx.Response
) -> Response[GetApiAdminScopesResourcesResponse200]:
    return Response(
        status_code=HTTPStatus(response.status_code),
        content=response.content,
        headers=response.headers,
        parsed=_parse_response(client=client, response=response),
    )


def sync_detailed(
    *,
    client: AuthenticatedClient,
) -> Response[GetApiAdminScopesResourcesResponse200]:
    """Scope-Resource Matrix

     #43 discovery -- the (path, method) -> required-scopes matrix from the central
    ResourceScopeRegistry. Read-only, no DB access.

    Raises:
        errors.UnexpectedStatus: If the server returns an undocumented status code and Client.raise_on_unexpected_status is True.
        httpx.TimeoutException: If the request takes longer than Client.timeout.

    Returns:
        Response[GetApiAdminScopesResourcesResponse200]
    """

    kwargs = _get_kwargs()

    response = client.get_httpx_client().request(
        **kwargs,
    )

    return _build_response(client=client, response=response)


def sync(
    *,
    client: AuthenticatedClient,
) -> GetApiAdminScopesResourcesResponse200 | None:
    """Scope-Resource Matrix

     #43 discovery -- the (path, method) -> required-scopes matrix from the central
    ResourceScopeRegistry. Read-only, no DB access.

    Raises:
        errors.UnexpectedStatus: If the server returns an undocumented status code and Client.raise_on_unexpected_status is True.
        httpx.TimeoutException: If the request takes longer than Client.timeout.

    Returns:
        GetApiAdminScopesResourcesResponse200
    """

    return sync_detailed(
        client=client,
    ).parsed


async def asyncio_detailed(
    *,
    client: AuthenticatedClient,
) -> Response[GetApiAdminScopesResourcesResponse200]:
    """Scope-Resource Matrix

     #43 discovery -- the (path, method) -> required-scopes matrix from the central
    ResourceScopeRegistry. Read-only, no DB access.

    Raises:
        errors.UnexpectedStatus: If the server returns an undocumented status code and Client.raise_on_unexpected_status is True.
        httpx.TimeoutException: If the request takes longer than Client.timeout.

    Returns:
        Response[GetApiAdminScopesResourcesResponse200]
    """

    kwargs = _get_kwargs()

    response = await client.get_async_httpx_client().request(**kwargs)

    return _build_response(client=client, response=response)


async def asyncio(
    *,
    client: AuthenticatedClient,
) -> GetApiAdminScopesResourcesResponse200 | None:
    """Scope-Resource Matrix

     #43 discovery -- the (path, method) -> required-scopes matrix from the central
    ResourceScopeRegistry. Read-only, no DB access.

    Raises:
        errors.UnexpectedStatus: If the server returns an undocumented status code and Client.raise_on_unexpected_status is True.
        httpx.TimeoutException: If the request takes longer than Client.timeout.

    Returns:
        GetApiAdminScopesResourcesResponse200
    """

    return (
        await asyncio_detailed(
            client=client,
        )
    ).parsed
