from __future__ import annotations

from collections.abc import Mapping
from typing import Any, TypeVar, cast

from attrs import define as _attrs_define
from attrs import field as _attrs_field

from ..types import UNSET, Unset

T = TypeVar("T", bound="PostOauth2MfaVerifyResponse200")


@_attrs_define
class PostOauth2MfaVerifyResponse200:
    """
    Attributes:
        access_token (str):
        token_type (str): Always "Bearer".
        expires_in (int): Seconds until access_token expiry.
        refresh_token (str | Unset): Present for grants that issue refresh tokens (rotated on refresh).
        scope (str | Unset): Space-separated scopes; echoed for client_credentials/device grants only.
        roles (list[str] | Unset): Role names; authorization_code grant only.
        id_token (str | Unset): OIDC ID token (JWT); present iff scope includes openid and signing keys are configured.
        message (str | Unset):
        mfa_verified (bool | Unset):
    """

    access_token: str
    token_type: str
    expires_in: int
    refresh_token: str | Unset = UNSET
    scope: str | Unset = UNSET
    roles: list[str] | Unset = UNSET
    id_token: str | Unset = UNSET
    message: str | Unset = UNSET
    mfa_verified: bool | Unset = UNSET
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

        message = self.message

        mfa_verified = self.mfa_verified

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
        if message is not UNSET:
            field_dict["message"] = message
        if mfa_verified is not UNSET:
            field_dict["mfa_verified"] = mfa_verified

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

        message = d.pop("message", UNSET)

        mfa_verified = d.pop("mfa_verified", UNSET)

        post_oauth_2_mfa_verify_response_200 = cls(
            access_token=access_token,
            token_type=token_type,
            expires_in=expires_in,
            refresh_token=refresh_token,
            scope=scope,
            roles=roles,
            id_token=id_token,
            message=message,
            mfa_verified=mfa_verified,
        )

        post_oauth_2_mfa_verify_response_200.additional_properties = d
        return post_oauth_2_mfa_verify_response_200

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
