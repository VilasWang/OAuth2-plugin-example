from __future__ import annotations

from collections.abc import Mapping
from typing import Any, TypeVar, cast

from attrs import define as _attrs_define
from attrs import field as _attrs_field

from ..types import UNSET, Unset

T = TypeVar("T", bound="PostApiMeMfaVerifyResponse200")


@_attrs_define
class PostApiMeMfaVerifyResponse200:
    """
    Attributes:
        message (str | Unset):
        backup_codes (list[str] | Unset): 10 single-use backup codes.
        warning (str | Unset):
    """

    message: str | Unset = UNSET
    backup_codes: list[str] | Unset = UNSET
    warning: str | Unset = UNSET
    additional_properties: dict[str, Any] = _attrs_field(init=False, factory=dict)

    def to_dict(self) -> dict[str, Any]:
        message = self.message

        backup_codes: list[str] | Unset = UNSET
        if not isinstance(self.backup_codes, Unset):
            backup_codes = self.backup_codes

        warning = self.warning

        field_dict: dict[str, Any] = {}
        field_dict.update(self.additional_properties)
        field_dict.update({})
        if message is not UNSET:
            field_dict["message"] = message
        if backup_codes is not UNSET:
            field_dict["backup_codes"] = backup_codes
        if warning is not UNSET:
            field_dict["warning"] = warning

        return field_dict

    @classmethod
    def from_dict(cls: type[T], src_dict: Mapping[str, Any]) -> T:
        d = dict(src_dict)
        message = d.pop("message", UNSET)

        backup_codes = cast(list[str], d.pop("backup_codes", UNSET))

        warning = d.pop("warning", UNSET)

        post_api_me_mfa_verify_response_200 = cls(
            message=message,
            backup_codes=backup_codes,
            warning=warning,
        )

        post_api_me_mfa_verify_response_200.additional_properties = d
        return post_api_me_mfa_verify_response_200

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
