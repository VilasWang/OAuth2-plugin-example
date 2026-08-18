from __future__ import annotations

from collections.abc import Mapping
from typing import Any, TypeVar, cast

from attrs import define as _attrs_define
from attrs import field as _attrs_field

from ..types import UNSET, Unset

T = TypeVar("T", bound="PutApiAdminUsersUserIdBody")


@_attrs_define
class PutApiAdminUsersUserIdBody:
    """
    Attributes:
        username (str | Unset):
        email (str | Unset):
        email_verified (bool | Unset):
        mfa_enabled (bool | Unset):
        locked (bool | Unset):
        org_id (int | None | Unset):
    """

    username: str | Unset = UNSET
    email: str | Unset = UNSET
    email_verified: bool | Unset = UNSET
    mfa_enabled: bool | Unset = UNSET
    locked: bool | Unset = UNSET
    org_id: int | None | Unset = UNSET
    additional_properties: dict[str, Any] = _attrs_field(init=False, factory=dict)

    def to_dict(self) -> dict[str, Any]:
        username = self.username

        email = self.email

        email_verified = self.email_verified

        mfa_enabled = self.mfa_enabled

        locked = self.locked

        org_id: int | None | Unset
        if isinstance(self.org_id, Unset):
            org_id = UNSET
        else:
            org_id = self.org_id

        field_dict: dict[str, Any] = {}
        field_dict.update(self.additional_properties)
        field_dict.update({})
        if username is not UNSET:
            field_dict["username"] = username
        if email is not UNSET:
            field_dict["email"] = email
        if email_verified is not UNSET:
            field_dict["email_verified"] = email_verified
        if mfa_enabled is not UNSET:
            field_dict["mfa_enabled"] = mfa_enabled
        if locked is not UNSET:
            field_dict["locked"] = locked
        if org_id is not UNSET:
            field_dict["org_id"] = org_id

        return field_dict

    @classmethod
    def from_dict(cls: type[T], src_dict: Mapping[str, Any]) -> T:
        d = dict(src_dict)
        username = d.pop("username", UNSET)

        email = d.pop("email", UNSET)

        email_verified = d.pop("email_verified", UNSET)

        mfa_enabled = d.pop("mfa_enabled", UNSET)

        locked = d.pop("locked", UNSET)

        def _parse_org_id(data: object) -> int | None | Unset:
            if data is None:
                return data
            if isinstance(data, Unset):
                return data
            return cast(int | None | Unset, data)

        org_id = _parse_org_id(d.pop("org_id", UNSET))

        put_api_admin_users_user_id_body = cls(
            username=username,
            email=email,
            email_verified=email_verified,
            mfa_enabled=mfa_enabled,
            locked=locked,
            org_id=org_id,
        )

        put_api_admin_users_user_id_body.additional_properties = d
        return put_api_admin_users_user_id_body

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
