from http import HTTPStatus
from typing import Any, cast

import httpx

from ... import errors
from ...client import AuthenticatedClient, Client
from ...models.error_envelope import ErrorEnvelope
from ...models.message_response import MessageResponse
from ...types import UNSET, Response, Unset


def _get_kwargs(
    *,
    id_token_hint: str | Unset = UNSET,
    post_logout_redirect_uri: str | Unset = UNSET,
    state: str | Unset = UNSET,
) -> dict[str, Any]:

    params: dict[str, Any] = {}

    params["id_token_hint"] = id_token_hint

    params["post_logout_redirect_uri"] = post_logout_redirect_uri

    params["state"] = state

    params = {k: v for k, v in params.items() if v is not UNSET and v is not None}

    _kwargs: dict[str, Any] = {
        "method": "post",
        "url": "/oauth2/end_session",
        "params": params,
    }

    return _kwargs


def _parse_response(
    *, client: AuthenticatedClient | Client, response: httpx.Response
) -> Any | ErrorEnvelope | MessageResponse | None:
    if response.status_code == 200:
        response_200 = MessageResponse.from_dict(response.json())

        return response_200

    if response.status_code == 302:
        response_302 = cast(Any, None)
        return response_302

    if response.status_code == 400:
        response_400 = ErrorEnvelope.from_dict(response.json())

        return response_400

    if client.raise_on_unexpected_status:
        raise errors.UnexpectedStatus(response.status_code, response.content)
    else:
        return None


def _build_response(
    *, client: AuthenticatedClient | Client, response: httpx.Response
) -> Response[Any | ErrorEnvelope | MessageResponse]:
    return Response(
        status_code=HTTPStatus(response.status_code),
        content=response.content,
        headers=response.headers,
        parsed=_parse_response(client=client, response=response),
    )


def sync_detailed(
    *,
    client: AuthenticatedClient | Client,
    id_token_hint: str | Unset = UNSET,
    post_logout_redirect_uri: str | Unset = UNSET,
    state: str | Unset = UNSET,
) -> Response[Any | ErrorEnvelope | MessageResponse]:
    """RP-Initiated Logout (POST)

     OIDC RP-Initiated Logout (POST form-based variant; see GET for semantics). id_token_hint, when
    supplied, is signature-verified (#78); verification failure yields 400 AUTH_INVALID_ID_TOKEN_HINT
    Error Envelope.

    Args:
        id_token_hint (str | Unset):
        post_logout_redirect_uri (str | Unset):
        state (str | Unset):

    Raises:
        errors.UnexpectedStatus: If the server returns an undocumented status code and Client.raise_on_unexpected_status is True.
        httpx.TimeoutException: If the request takes longer than Client.timeout.

    Returns:
        Response[Any | ErrorEnvelope | MessageResponse]
    """

    kwargs = _get_kwargs(
        id_token_hint=id_token_hint,
        post_logout_redirect_uri=post_logout_redirect_uri,
        state=state,
    )

    response = client.get_httpx_client().request(
        **kwargs,
    )

    return _build_response(client=client, response=response)


def sync(
    *,
    client: AuthenticatedClient | Client,
    id_token_hint: str | Unset = UNSET,
    post_logout_redirect_uri: str | Unset = UNSET,
    state: str | Unset = UNSET,
) -> Any | ErrorEnvelope | MessageResponse | None:
    """RP-Initiated Logout (POST)

     OIDC RP-Initiated Logout (POST form-based variant; see GET for semantics). id_token_hint, when
    supplied, is signature-verified (#78); verification failure yields 400 AUTH_INVALID_ID_TOKEN_HINT
    Error Envelope.

    Args:
        id_token_hint (str | Unset):
        post_logout_redirect_uri (str | Unset):
        state (str | Unset):

    Raises:
        errors.UnexpectedStatus: If the server returns an undocumented status code and Client.raise_on_unexpected_status is True.
        httpx.TimeoutException: If the request takes longer than Client.timeout.

    Returns:
        Any | ErrorEnvelope | MessageResponse
    """

    return sync_detailed(
        client=client,
        id_token_hint=id_token_hint,
        post_logout_redirect_uri=post_logout_redirect_uri,
        state=state,
    ).parsed


async def asyncio_detailed(
    *,
    client: AuthenticatedClient | Client,
    id_token_hint: str | Unset = UNSET,
    post_logout_redirect_uri: str | Unset = UNSET,
    state: str | Unset = UNSET,
) -> Response[Any | ErrorEnvelope | MessageResponse]:
    """RP-Initiated Logout (POST)

     OIDC RP-Initiated Logout (POST form-based variant; see GET for semantics). id_token_hint, when
    supplied, is signature-verified (#78); verification failure yields 400 AUTH_INVALID_ID_TOKEN_HINT
    Error Envelope.

    Args:
        id_token_hint (str | Unset):
        post_logout_redirect_uri (str | Unset):
        state (str | Unset):

    Raises:
        errors.UnexpectedStatus: If the server returns an undocumented status code and Client.raise_on_unexpected_status is True.
        httpx.TimeoutException: If the request takes longer than Client.timeout.

    Returns:
        Response[Any | ErrorEnvelope | MessageResponse]
    """

    kwargs = _get_kwargs(
        id_token_hint=id_token_hint,
        post_logout_redirect_uri=post_logout_redirect_uri,
        state=state,
    )

    response = await client.get_async_httpx_client().request(**kwargs)

    return _build_response(client=client, response=response)


async def asyncio(
    *,
    client: AuthenticatedClient | Client,
    id_token_hint: str | Unset = UNSET,
    post_logout_redirect_uri: str | Unset = UNSET,
    state: str | Unset = UNSET,
) -> Any | ErrorEnvelope | MessageResponse | None:
    """RP-Initiated Logout (POST)

     OIDC RP-Initiated Logout (POST form-based variant; see GET for semantics). id_token_hint, when
    supplied, is signature-verified (#78); verification failure yields 400 AUTH_INVALID_ID_TOKEN_HINT
    Error Envelope.

    Args:
        id_token_hint (str | Unset):
        post_logout_redirect_uri (str | Unset):
        state (str | Unset):

    Raises:
        errors.UnexpectedStatus: If the server returns an undocumented status code and Client.raise_on_unexpected_status is True.
        httpx.TimeoutException: If the request takes longer than Client.timeout.

    Returns:
        Any | ErrorEnvelope | MessageResponse
    """

    return (
        await asyncio_detailed(
            client=client,
            id_token_hint=id_token_hint,
            post_logout_redirect_uri=post_logout_redirect_uri,
            state=state,
        )
    ).parsed
