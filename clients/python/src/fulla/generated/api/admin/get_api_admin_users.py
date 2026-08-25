from http import HTTPStatus
from typing import Any

import httpx

from ... import errors
from ...client import AuthenticatedClient, Client
from ...models.get_api_admin_users_locked import GetApiAdminUsersLocked
from ...types import UNSET, Response, Unset


def _get_kwargs(
    *,
    page: int | Unset = 1,
    per_page: int | Unset = 50,
    q: str | Unset = UNSET,
    role: str | Unset = UNSET,
    locked: GetApiAdminUsersLocked | Unset = UNSET,
) -> dict[str, Any]:

    params: dict[str, Any] = {}

    params["page"] = page

    params["per_page"] = per_page

    params["q"] = q

    params["role"] = role

    json_locked: str | Unset = UNSET
    if not isinstance(locked, Unset):
        json_locked = locked.value

    params["locked"] = json_locked

    params = {k: v for k, v in params.items() if v is not UNSET and v is not None}

    _kwargs: dict[str, Any] = {
        "method": "get",
        "url": "/api/admin/users",
        "params": params,
    }

    return _kwargs


def _parse_response(*, client: AuthenticatedClient | Client, response: httpx.Response) -> Any | None:
    if response.status_code == 200:
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
    page: int | Unset = 1,
    per_page: int | Unset = 50,
    q: str | Unset = UNSET,
    role: str | Unset = UNSET,
    locked: GetApiAdminUsersLocked | Unset = UNSET,
) -> Response[Any]:
    """List Users

     List users with optional pagination (page, per_page) and filtering (q for username/email prefix,
    role, locked). Returns total count.

    Args:
        page (int | Unset):  Default: 1.
        per_page (int | Unset):  Default: 50.
        q (str | Unset):
        role (str | Unset):
        locked (GetApiAdminUsersLocked | Unset):

    Raises:
        errors.UnexpectedStatus: If the server returns an undocumented status code and Client.raise_on_unexpected_status is True.
        httpx.TimeoutException: If the request takes longer than Client.timeout.

    Returns:
        Response[Any]
    """

    kwargs = _get_kwargs(
        page=page,
        per_page=per_page,
        q=q,
        role=role,
        locked=locked,
    )

    response = client.get_httpx_client().request(
        **kwargs,
    )

    return _build_response(client=client, response=response)


async def asyncio_detailed(
    *,
    client: AuthenticatedClient,
    page: int | Unset = 1,
    per_page: int | Unset = 50,
    q: str | Unset = UNSET,
    role: str | Unset = UNSET,
    locked: GetApiAdminUsersLocked | Unset = UNSET,
) -> Response[Any]:
    """List Users

     List users with optional pagination (page, per_page) and filtering (q for username/email prefix,
    role, locked). Returns total count.

    Args:
        page (int | Unset):  Default: 1.
        per_page (int | Unset):  Default: 50.
        q (str | Unset):
        role (str | Unset):
        locked (GetApiAdminUsersLocked | Unset):

    Raises:
        errors.UnexpectedStatus: If the server returns an undocumented status code and Client.raise_on_unexpected_status is True.
        httpx.TimeoutException: If the request takes longer than Client.timeout.

    Returns:
        Response[Any]
    """

    kwargs = _get_kwargs(
        page=page,
        per_page=per_page,
        q=q,
        role=role,
        locked=locked,
    )

    response = await client.get_async_httpx_client().request(**kwargs)

    return _build_response(client=client, response=response)
