from __future__ import annotations

from collections.abc import Mapping
from typing import Any, TypeVar, cast

from attrs import define as _attrs_define
from attrs import field as _attrs_field

from ..types import UNSET, Unset

T = TypeVar("T", bound="TokenResponse")


@_attrs_define
class TokenResponse:
    """Token endpoint success body (grant-dependent fields). authorization_code:
    access_token/token_type/expires_in/refresh_token/roles + id_token iff scope includes openid (no scope echo).
    refresh_token: rotated refresh_token, no roles. client_credentials: scope echo, NO refresh_token. device_code:
    refresh_token + optional scope/id_token. Always carries Cache-Control: no-store / Pragma: no-cache.

        Attributes:
            access_token (str):
            token_type (str): Always "Bearer".
            expires_in (int): Seconds until access_token expiry.
            refresh_token (str | Unset): Present for grants that issue refresh tokens (rotated on refresh).
            scope (str | Unset): Space-separated scopes; echoed for client_credentials/device grants only.
            roles (list[str] | Unset): Role names; authorization_code grant only.
            id_token (str | Unset): OIDC ID token (JWT); present iff scope includes openid and signing keys are configured.
    """

    access_token: str
    token_type: str
    expires_in: int
    refresh_token: str | Unset = UNSET
    scope: str | Unset = UNSET
    roles: list[str] | Unset = UNSET
    id_token: str | Unset = UNSET
    additional_properties: dict[str, Any] = _attrs_field(init=False, factory=dict)

    def to_dict(self) -> dict[str, Any]:
        access_token = self.access_token

        token_type = self.token_type

        expires_in = self.expires_in

        refresh_token = self.refresh_token

        scope = self.scope

        roles: list[str] | Unset = UNSET
        if not isinstance(self.roles, Unset):
            roles = self.roles

        id_token = self.id_token

        field_dict: dict[str, Any] = {}
        field_dict.update(self.additional_properties)
        field_dict.update(
            {
                "access_token": access_token,
                "token_type": token_type,
                "expires_in": expires_in,
            }
        )
        if refresh_token is not UNSET:
            field_dict["refresh_token"] = refresh_token
        if scope is not UNSET:
            field_dict["scope"] = scope
        if roles is not UNSET:
            field_dict["roles"] = roles
        if id_token is not UNSET:
            field_dict["id_token"] = id_token

        return field_dict

    @classmethod
    def from_dict(cls: type[T], src_dict: Mapping[str, Any]) -> T:
        d = dict(src_dict)
        access_token = d.pop("access_token")

        token_type = d.pop("token_type")

        expires_in = d.pop("expires_in")

        refresh_token = d.pop("refresh_token", UNSET)

        scope = d.pop("scope", UNSET)

        roles = cast(list[str], d.pop("roles", UNSET))

        id_token = d.pop("id_token", UNSET)

        token_response = cls(
            access_token=access_token,
            token_type=token_type,
            expires_in=expires_in,
            refresh_token=refresh_token,
            scope=scope,
            roles=roles,
            id_token=id_token,
        )

        token_response.additional_properties = d
        return token_response

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
