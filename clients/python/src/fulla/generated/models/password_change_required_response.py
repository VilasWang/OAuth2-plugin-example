from __future__ import annotations

from collections.abc import Mapping
from typing import Any, TypeVar

from attrs import define as _attrs_define
from attrs import field as _attrs_field

from ..types import UNSET, Unset

T = TypeVar("T", bound="PasswordChangeRequiredResponse")


@_attrs_define
class PasswordChangeRequiredResponse:
    """/oauth2/login response while the account is flagged must_change_password (#145: bootstrap admin, admin-created
    users). No authorization code is issued until the password is changed via POST /oauth2/password/change.

        Attributes:
            password_change_required (bool):
            message (str | Unset):
    """

    password_change_required: bool
    message: str | Unset = UNSET
    additional_properties: dict[str, Any] = _attrs_field(init=False, factory=dict)

    def to_dict(self) -> dict[str, Any]:
        password_change_required = self.password_change_required

        message = self.message

        field_dict: dict[str, Any] = {}
        field_dict.update(self.additional_properties)
        field_dict.update(
            {
                "password_change_required": password_change_required,
            }
        )
        if message is not UNSET:
            field_dict["message"] = message

        return field_dict

    @classmethod
    def from_dict(cls: type[T], src_dict: Mapping[str, Any]) -> T:
        d = dict(src_dict)
        password_change_required = d.pop("password_change_required")

        message = d.pop("message", UNSET)

        password_change_required_response = cls(
            password_change_required=password_change_required,
            message=message,
        )

        password_change_required_response.additional_properties = d
        return password_change_required_response

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
