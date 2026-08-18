from __future__ import annotations

from collections.abc import Mapping
from typing import Any, TypeVar

from attrs import define as _attrs_define
from attrs import field as _attrs_field

from ..models.token_request_grant_type import TokenRequestGrantType
from ..types import UNSET, Unset

T = TypeVar("T", bound="TokenRequest")


@_attrs_define
class TokenRequest:
    """RFC 6749 §4.1.3 token request (form-encoded). Field requirements are grant-dependent: authorization_code needs code
    (+ redirect_uri + code_verifier when PKCE was used); refresh_token needs refresh_token; client_credentials may send
    scope; device_code needs device_code. CONFIDENTIAL clients authenticate via HTTP Basic or client_id + client_secret
    fields.

        Attributes:
            grant_type (TokenRequestGrantType):
            code (str | Unset): Authorization code (required for grant_type=authorization_code).
            redirect_uri (str | Unset): Required for the authorization_code grant.
            refresh_token (str | Unset): Required for grant_type=refresh_token (rotated on use).
            code_verifier (str | Unset): PKCE code_verifier (RFC 7636): required for authorization_code when the
                login/authorize request carried a code_challenge.
            client_id (str | Unset): Client identifier (alternative to HTTP Basic authentication).
            client_secret (str | Unset): Client secret for CONFIDENTIAL clients with
                token_endpoint_auth_method=client_secret_post; FORBIDDEN for PUBLIC clients whose method is 'none' (F-017).
            scope (str | Unset): Space-separated scopes (client_credentials grant only).
            device_code (str | Unset): Required for the device_code grant (from /oauth2/device_authorization).
    """

    grant_type: TokenRequestGrantType
    code: str | Unset = UNSET
    redirect_uri: str | Unset = UNSET
    refresh_token: str | Unset = UNSET
    code_verifier: str | Unset = UNSET
    client_id: str | Unset = UNSET
    client_secret: str | Unset = UNSET
    scope: str | Unset = UNSET
    device_code: str | Unset = UNSET
    additional_properties: dict[str, Any] = _attrs_field(init=False, factory=dict)

    def to_dict(self) -> dict[str, Any]:
        grant_type = self.grant_type.value

        code = self.code

        redirect_uri = self.redirect_uri

        refresh_token = self.refresh_token

        code_verifier = self.code_verifier

        client_id = self.client_id

        client_secret = self.client_secret

        scope = self.scope

        device_code = self.device_code

        field_dict: dict[str, Any] = {}
        field_dict.update(self.additional_properties)
        field_dict.update(
            {
                "grant_type": grant_type,
            }
        )
        if code is not UNSET:
            field_dict["code"] = code
        if redirect_uri is not UNSET:
            field_dict["redirect_uri"] = redirect_uri
        if refresh_token is not UNSET:
            field_dict["refresh_token"] = refresh_token
        if code_verifier is not UNSET:
            field_dict["code_verifier"] = code_verifier
        if client_id is not UNSET:
            field_dict["client_id"] = client_id
        if client_secret is not UNSET:
            field_dict["client_secret"] = client_secret
        if scope is not UNSET:
            field_dict["scope"] = scope
        if device_code is not UNSET:
            field_dict["device_code"] = device_code

        return field_dict

    @classmethod
    def from_dict(cls: type[T], src_dict: Mapping[str, Any]) -> T:
        d = dict(src_dict)
        grant_type = TokenRequestGrantType(d.pop("grant_type"))

        code = d.pop("code", UNSET)

        redirect_uri = d.pop("redirect_uri", UNSET)

        refresh_token = d.pop("refresh_token", UNSET)

        code_verifier = d.pop("code_verifier", UNSET)

        client_id = d.pop("client_id", UNSET)

        client_secret = d.pop("client_secret", UNSET)

        scope = d.pop("scope", UNSET)

        device_code = d.pop("device_code", UNSET)

        token_request = cls(
            grant_type=grant_type,
            code=code,
            redirect_uri=redirect_uri,
            refresh_token=refresh_token,
            code_verifier=code_verifier,
            client_id=client_id,
            client_secret=client_secret,
            scope=scope,
            device_code=device_code,
        )

        token_request.additional_properties = d
        return token_request

    @property
    def additional_keys(self) -> list[str]:
        return list(self.additional_properties.keys())

    def __getitem__(self, key: str) -> Any:
        return self.additional_properties[key]

    def __setitem__(self, key: str, value: Any) -> None:
        self.additional_properties[key] = value

    def __delitem__(self, key: str) -> None:
        del self.additional_properties[key]

    def __contains__(self, key: str) -> bool:
        return key in self.additional_properties
