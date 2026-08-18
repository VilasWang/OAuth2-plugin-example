from __future__ import annotations

from collections.abc import Mapping
from typing import Any, TypeVar

from attrs import define as _attrs_define
from attrs import field as _attrs_field

from ..types import UNSET, Unset

T = TypeVar("T", bound="PostOauth2DeviceApproveResponse200")


@_attrs_define
class PostOauth2DeviceApproveResponse200:
    """
    Attributes:
        status (str | Unset):  Example: approved.
        user_code (str | Unset):
    """

    status: str | Unset = UNSET
    user_code: str | Unset = UNSET
    additional_properties: dict[str, Any] = _attrs_field(init=False, factory=dict)

    def to_dict(self) -> dict[str, Any]:
        status = self.status

        user_code = self.user_code

        field_dict: dict[str, Any] = {}
        field_dict.update(self.additional_properties)
        field_dict.update({})
        if status is not UNSET:
            field_dict["status"] = status
        if user_code is not UNSET:
            field_dict["user_code"] = user_code

        return field_dict

    @classmethod
    def from_dict(cls: type[T], src_dict: Mapping[str, Any]) -> T:
        d = dict(src_dict)
        status = d.pop("status", UNSET)

        user_code = d.pop("user_code", UNSET)

        post_oauth_2_device_approve_response_200 = cls(
            status=status,
            user_code=user_code,
        )

        post_oauth_2_device_approve_response_200.additional_properties = d
        return post_oauth_2_device_approve_response_200

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
