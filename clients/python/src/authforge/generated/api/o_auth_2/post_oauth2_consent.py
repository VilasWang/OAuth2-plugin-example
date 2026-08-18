from http import HTTPStatus
from typing import Any

import httpx

from ... import errors
from ...client import AuthenticatedClient, Client
from ...models.post_oauth_2_consent_action import PostOauth2ConsentAction
from ...types import UNSET, Response, Unset


def _get_kwargs(
    *,
    client_id: str,
    user_id: str,
    scope: str,
    redirect_uri: str,
    state: str | Unset = UNSET,
    action: PostOauth2ConsentAction,
) -> dict[str, Any]:

    params: dict[str, Any] = {}

    params["client_id"] = client_id

    params["user_id"] = user_id

    params["scope"] = scope

    params["redirect_uri"] = redirect_uri

    params["state"] = state

    json_action = action.value
    params["action"] = json_action

    params = {k: v for k, v in params.items() if v is not UNSET and v is not None}

    _kwargs: dict[str, Any] = {
        "method": "post",
        "url": "/oauth2/consent",
        "params": params,
    }

    return _kwargs


def _parse_response(*, client: AuthenticatedClient | Client, response: httpx.Response) -> Any | None:
    if response.status_code == 302:
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
    client_id: str,
    user_id: str,
    scope: str,
    redirect_uri: str,
    state: str | Unset = UNSET,
    action: PostOauth2ConsentAction,
) -> Response[Any]:
    """Submit user consent

     Submit user consent for requested scopes. Redirects back to client.

    Args:
        client_id (str):
        user_id (str):
        scope (str):
        redirect_uri (str):
        state (str | Unset):
        action (PostOauth2ConsentAction):

    Raises:
        errors.UnexpectedStatus: If the server returns an undocumented status code and Client.raise_on_unexpected_status is True.
        httpx.TimeoutException: If the request takes longer than Client.timeout.

    Returns:
        Response[Any]
    """

    kwargs = _get_kwargs(
        client_id=client_id,
        user_id=user_id,
        scope=scope,
        redirect_uri=redirect_uri,
        state=state,
        action=action,
    )

    response = client.get_httpx_client().request(
        **kwargs,
    )

    return _build_response(client=client, response=response)


async def asyncio_detailed(
    *,
    client: AuthenticatedClient | Client,
    client_id: str,
    user_id: str,
    scope: str,
    redirect_uri: str,
    state: str | Unset = UNSET,
    action: PostOauth2ConsentAction,
) -> Response[Any]:
    """Submit user consent

     Submit user consent for requested scopes. Redirects back to client.

    Args:
        client_id (str):
        user_id (str):
        scope (str):
        redirect_uri (str):
        state (str | Unset):
        action (PostOauth2ConsentAction):

    Raises:
        errors.UnexpectedStatus: If the server returns an undocumented status code and Client.raise_on_unexpected_status is True.
        httpx.TimeoutException: If the request takes longer than Client.timeout.

    Returns:
        Response[Any]
    """

    kwargs = _get_kwargs(
        client_id=client_id,
        user_id=user_id,
        scope=scope,
        redirect_uri=redirect_uri,
        state=state,
        action=action,
    )

    response = await client.get_async_httpx_client().request(**kwargs)

    return _build_response(client=client, response=response)
