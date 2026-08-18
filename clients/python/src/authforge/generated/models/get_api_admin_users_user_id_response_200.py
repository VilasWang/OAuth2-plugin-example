from __future__ import annotations

from collections.abc import Mapping
from typing import Any, TypeVar, cast

from attrs import define as _attrs_define
from attrs import field as _attrs_field

from ..types import UNSET, Unset

T = TypeVar("T", bound="GetApiAdminUsersUserIdResponse200")


@_attrs_define
class GetApiAdminUsersUserIdResponse200:
    """
    Attributes:
        created_at (str | Unset):
        email (str | Unset):
        email_verified (bool | Unset):
        failed_login_count (int | Unset):
        id (int | Unset):
        locked (bool | Unset):
        locked_until (int | Unset):
        mfa_enabled (bool | Unset):
        org_id (int | None | Unset):
        roles (list[str] | Unset):
        status (str | Unset):
        username (str | Unset):
    """

    created_at: str | Unset = UNSET
    email: str | Unset = UNSET
    email_verified: bool | Unset = UNSET
    failed_login_count: int | Unset = UNSET
    id: int | Unset = UNSET
    locked: bool | Unset = UNSET
    locked_until: int | Unset = UNSET
    mfa_enabled: bool | Unset = UNSET
    org_id: int | None | Unset = UNSET
    roles: list[str] | Unset = UNSET
    status: str | Unset = UNSET
    username: str | Unset = UNSET
    additional_properties: dict[str, Any] = _attrs_field(init=False, factory=dict)

    def to_dict(self) -> dict[str, Any]:
        created_at = self.created_at

        email = self.email

        email_verified = self.email_verified

        failed_login_count = self.failed_login_count

        id = self.id

        locked = self.locked

        locked_until = self.locked_until

        mfa_enabled = self.mfa_enabled

        org_id: int | None | Unset
        if isinstance(self.org_id, Unset):
            org_id = UNSET
        else:
            org_id = self.org_id

        roles: list[str] | Unset = UNSET
        if not isinstance(self.roles, Unset):
            roles = self.roles

        status = self.status

        username = self.username

        field_dict: dict[str, Any] = {}
        field_dict.update(self.additional_properties)
        field_dict.update({})
        if created_at is not UNSET:
            field_dict["created_at"] = created_at
        if email is not UNSET:
            field_dict["email"] = email
        if email_verified is not UNSET:
            field_dict["email_verified"] = email_verified
        if failed_login_count is not UNSET:
            field_dict["failed_login_count"] = failed_login_count
        if id is not UNSET:
            field_dict["id"] = id
        if locked is not UNSET:
            field_dict["locked"] = locked
        if locked_until is not UNSET:
            field_dict["locked_until"] = locked_until
        if mfa_enabled is not UNSET:
            field_dict["mfa_enabled"] = mfa_enabled
        if org_id is not UNSET:
            field_dict["org_id"] = org_id
        if roles is not UNSET:
            field_dict["roles"] = roles
        if status is not UNSET:
            field_dict["status"] = status
        if username is not UNSET:
            field_dict["username"] = username

        return field_dict

    @classmethod
    def from_dict(cls: type[T], src_dict: Mapping[str, Any]) -> T:
        d = dict(src_dict)
        created_at = d.pop("created_at", UNSET)

        email = d.pop("email", UNSET)

        email_verified = d.pop("email_verified", UNSET)

        failed_login_count = d.pop("failed_login_count", UNSET)

        id = d.pop("id", UNSET)

        locked = d.pop("locked", UNSET)

        locked_until = d.pop("locked_until", UNSET)

        mfa_enabled = d.pop("mfa_enabled", UNSET)

        def _parse_org_id(data: object) -> int | None | Unset:
            if data is None:
                return data
            if isinstance(data, Unset):
                return data
            return cast(int | None | Unset, data)

        org_id = _parse_org_id(d.pop("org_id", UNSET))

        roles = cast(list[str], d.pop("roles", UNSET))

        status = d.pop("status", UNSET)

        username = d.pop("username", UNSET)

        get_api_admin_users_user_id_response_200 = cls(
            created_at=created_at,
            email=email,
            email_verified=email_verified,
            failed_login_count=failed_login_count,
            id=id,
            locked=locked,
            locked_until=locked_until,
            mfa_enabled=mfa_enabled,
            org_id=org_id,
            roles=roles,
            status=status,
            username=username,
        )

        get_api_admin_users_user_id_response_200.additional_properties = d
        return get_api_admin_users_user_id_response_200

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
