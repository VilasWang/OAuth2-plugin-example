from http import HTTPStatus
from typing import Any, cast

import httpx

from ... import errors
from ...client import AuthenticatedClient, Client
from ...models.error_envelope import ErrorEnvelope
from ...models.login_request import LoginRequest
from ...models.login_success_response import LoginSuccessResponse
from ...models.mfa_required_response import MfaRequiredResponse
from ...types import UNSET, Response


def _get_kwargs(
    *,
    body: LoginRequest | LoginRequest | Unset = UNSET,
) -> dict[str, Any]:
    headers: dict[str, Any] = {}

    _kwargs: dict[str, Any] = {
        "method": "post",
        "url": "/oauth2/login",
    }

    if isinstance(body, LoginRequest):
        _kwargs["json"] = body.to_dict()

        headers["Content-Type"] = "application/json"
    if isinstance(body, LoginRequest):
        _kwargs["data"] = body.to_dict()
        headers["Content-Type"] = "application/x-www-form-urlencoded"

    _kwargs["headers"] = headers
    return _kwargs


def _parse_response(
    *, client: AuthenticatedClient | Client, response: httpx.Response
) -> Any | ErrorEnvelope | LoginSuccessResponse | MfaRequiredResponse | None:
    if response.status_code == 200:

        def _parse_response_200(data: object) -> LoginSuccessResponse | MfaRequiredResponse:
            try:
                if not isinstance(data, dict):
                    raise TypeError()
                response_200_type_0 = LoginSuccessResponse.from_dict(data)

                return response_200_type_0
            except (TypeError, ValueError, AttributeError, KeyError):
                pass
            if not isinstance(data, dict):
                raise TypeError()
            response_200_type_1 = MfaRequiredResponse.from_dict(data)

            return response_200_type_1

        response_200 = _parse_response_200(response.json())

        return response_200

    if response.status_code == 302:
        response_302 = cast(Any, None)
        return response_302

    if response.status_code == 400:
        response_400 = ErrorEnvelope.from_dict(response.json())

        return response_400

    if response.status_code == 401:
        response_401 = ErrorEnvelope.from_dict(response.json())

        return response_401

    if response.status_code == 403:
        response_403 = ErrorEnvelope.from_dict(response.json())

        return response_403

    if client.raise_on_unexpected_status:
        raise errors.UnexpectedStatus(response.status_code, response.content)
    else:
        return None


def _build_response(
    *, client: AuthenticatedClient | Client, response: httpx.Response
) -> Response[Any | ErrorEnvelope | LoginSuccessResponse | MfaRequiredResponse]:
    return Response(
        status_code=HTTPStatus(response.status_code),
        content=response.content,
        headers=response.headers,
        parsed=_parse_response(client=client, response=response),
    )


def sync_detailed(
    *,
    client: AuthenticatedClient | Client,
    body: LoginRequest | LoginRequest | Unset = UNSET,
) -> Response[Any | ErrorEnvelope | LoginSuccessResponse | MfaRequiredResponse]:
    """Authenticate user

     Authenticates user credentials and generates an authorization code. Usually called by the frontend
    login page during the authorization code flow. Accepts a JSON body or form-encoded parameters; when
    MFA is enabled on the account the response defers to POST /oauth2/mfa/verify.

    Args:
        body (LoginRequest): /oauth2/login credentials (JSON body or form-encoded; query also
            accepted).
        body (LoginRequest): /oauth2/login credentials (JSON body or form-encoded; query also
            accepted).

    Raises:
        errors.UnexpectedStatus: If the server returns an undocumented status code and Client.raise_on_unexpected_status is True.
        httpx.TimeoutException: If the request takes longer than Client.timeout.

    Returns:
        Response[Any | ErrorEnvelope | LoginSuccessResponse | MfaRequiredResponse]
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
    body: LoginRequest | LoginRequest | Unset = UNSET,
) -> Any | ErrorEnvelope | LoginSuccessResponse | MfaRequiredResponse | None:
    """Authenticate user

     Authenticates user credentials and generates an authorization code. Usually called by the frontend
    login page during the authorization code flow. Accepts a JSON body or form-encoded parameters; when
    MFA is enabled on the account the response defers to POST /oauth2/mfa/verify.

    Args:
        body (LoginRequest): /oauth2/login credentials (JSON body or form-encoded; query also
            accepted).
        body (LoginRequest): /oauth2/login credentials (JSON body or form-encoded; query also
            accepted).

    Raises:
        errors.UnexpectedStatus: If the server returns an undocumented status code and Client.raise_on_unexpected_status is True.
        httpx.TimeoutException: If the request takes longer than Client.timeout.

    Returns:
        Any | ErrorEnvelope | LoginSuccessResponse | MfaRequiredResponse
    """

    return sync_detailed(
        client=client,
        body=body,
    ).parsed


async def asyncio_detailed(
    *,
    client: AuthenticatedClient | Client,
    body: LoginRequest | LoginRequest | Unset = UNSET,
) -> Response[Any | ErrorEnvelope | LoginSuccessResponse | MfaRequiredResponse]:
    """Authenticate user

     Authenticates user credentials and generates an authorization code. Usually called by the frontend
    login page during the authorization code flow. Accepts a JSON body or form-encoded parameters; when
    MFA is enabled on the account the response defers to POST /oauth2/mfa/verify.

    Args:
        body (LoginRequest): /oauth2/login credentials (JSON body or form-encoded; query also
            accepted).
        body (LoginRequest): /oauth2/login credentials (JSON body or form-encoded; query also
            accepted).

    Raises:
        errors.UnexpectedStatus: If the server returns an undocumented status code and Client.raise_on_unexpected_status is True.
        httpx.TimeoutException: If the request takes longer than Client.timeout.

    Returns:
        Response[Any | ErrorEnvelope | LoginSuccessResponse | MfaRequiredResponse]
    """

    kwargs = _get_kwargs(
        body=body,
    )

    response = await client.get_async_httpx_client().request(**kwargs)

    return _build_response(client=client, response=response)


async def asyncio(
    *,
    client: AuthenticatedClient | Client,
    body: LoginRequest | LoginRequest | Unset = UNSET,
) -> Any | ErrorEnvelope | LoginSuccessResponse | MfaRequiredResponse | None:
    """Authenticate user

     Authenticates user credentials and generates an authorization code. Usually called by the frontend
    login page during the authorization code flow. Accepts a JSON body or form-encoded parameters; when
    MFA is enabled on the account the response defers to POST /oauth2/mfa/verify.

    Args:
        body (LoginRequest): /oauth2/login credentials (JSON body or form-encoded; query also
            accepted).
        body (LoginRequest): /oauth2/login credentials (JSON body or form-encoded; query also
            accepted).

    Raises:
        errors.UnexpectedStatus: If the server returns an undocumented status code and Client.raise_on_unexpected_status is True.
        httpx.TimeoutException: If the request takes longer than Client.timeout.

    Returns:
        Any | ErrorEnvelope | LoginSuccessResponse | MfaRequiredResponse
    """

    return (
        await asyncio_detailed(
            client=client,
            body=body,
        )
    ).parsed
