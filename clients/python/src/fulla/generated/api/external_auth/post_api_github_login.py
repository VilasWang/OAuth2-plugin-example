from http import HTTPStatus
from typing import Any, cast

import httpx

from ... import errors
from ...client import AuthenticatedClient, Client
from ...models.post_api_github_login_response_200 import PostApiGithubLoginResponse200
from ...types import UNSET, Response


def _get_kwargs(
    *,
    code: str,
) -> dict[str, Any]:

    params: dict[str, Any] = {}

    params["code"] = code

    params = {k: v for k, v in params.items() if v is not UNSET and v is not None}

    _kwargs: dict[str, Any] = {
        "method": "post",
        "url": "/api/github/login",
        "params": params,
    }

    return _kwargs


def _parse_response(
    *, client: AuthenticatedClient | Client, response: httpx.Response
) -> Any | PostApiGithubLoginResponse200 | None:
    if response.status_code == 200:
        response_200 = PostApiGithubLoginResponse200.from_dict(response.json())

        return response_200

    if response.status_code == 400:
        response_400 = cast(Any, None)
        return response_400

    if response.status_code == 502:
        response_502 = cast(Any, None)
        return response_502

    if client.raise_on_unexpected_status:
        raise errors.UnexpectedStatus(response.status_code, response.content)
    else:
        return None


def _build_response(
    *, client: AuthenticatedClient | Client, response: httpx.Response
) -> Response[Any | PostApiGithubLoginResponse200]:
    return Response(
        status_code=HTTPStatus(response.status_code),
        content=response.content,
        headers=response.headers,
        parsed=_parse_response(client=client, response=response),
    )


def sync_detailed(
    *,
    client: AuthenticatedClient | Client,
    code: str,
) -> Response[Any | PostApiGithubLoginResponse200]:
    """GitHub OAuth2 Login

     Exchange GitHub authorization code for user information. This endpoint handles the server-side
    OAuth2 flow with GitHub.

    Args:
        code (str):

    Raises:
        errors.UnexpectedStatus: If the server returns an undocumented status code and Client.raise_on_unexpected_status is True.
        httpx.TimeoutException: If the request takes longer than Client.timeout.

    Returns:
        Response[Any | PostApiGithubLoginResponse200]
    """

    kwargs = _get_kwargs(
        code=code,
    )

    response = client.get_httpx_client().request(
        **kwargs,
    )

    return _build_response(client=client, response=response)


def sync(
    *,
    client: AuthenticatedClient | Client,
    code: str,
) -> Any | PostApiGithubLoginResponse200 | None:
    """GitHub OAuth2 Login

     Exchange GitHub authorization code for user information. This endpoint handles the server-side
    OAuth2 flow with GitHub.

    Args:
        code (str):

    Raises:
        errors.UnexpectedStatus: If the server returns an undocumented status code and Client.raise_on_unexpected_status is True.
        httpx.TimeoutException: If the request takes longer than Client.timeout.

    Returns:
        Any | PostApiGithubLoginResponse200
    """

    return sync_detailed(
        client=client,
        code=code,
    ).parsed


async def asyncio_detailed(
    *,
    client: AuthenticatedClient | Client,
    code: str,
) -> Response[Any | PostApiGithubLoginResponse200]:
    """GitHub OAuth2 Login

     Exchange GitHub authorization code for user information. This endpoint handles the server-side
    OAuth2 flow with GitHub.

    Args:
        code (str):

    Raises:
        errors.UnexpectedStatus: If the server returns an undocumented status code and Client.raise_on_unexpected_status is True.
        httpx.TimeoutException: If the request takes longer than Client.timeout.

    Returns:
        Response[Any | PostApiGithubLoginResponse200]
    """

    kwargs = _get_kwargs(
        code=code,
    )

    response = await client.get_async_httpx_client().request(**kwargs)

    return _build_response(client=client, response=response)


async def asyncio(
    *,
    client: AuthenticatedClient | Client,
    code: str,
) -> Any | PostApiGithubLoginResponse200 | None:
    """GitHub OAuth2 Login

     Exchange GitHub authorization code for user information. This endpoint handles the server-side
    OAuth2 flow with GitHub.

    Args:
        code (str):

    Raises:
        errors.UnexpectedStatus: If the server returns an undocumented status code and Client.raise_on_unexpected_status is True.
        httpx.TimeoutException: If the request takes longer than Client.timeout.

    Returns:
        Any | PostApiGithubLoginResponse200
    """

    return (
        await asyncio_detailed(
            client=client,
            code=code,
        )
    ).parsed
