from __future__ import annotations

from collections.abc import Mapping
from typing import Any, TypeVar, cast

from attrs import define as _attrs_define
from attrs import field as _attrs_field

from ..types import UNSET, Unset

T = TypeVar("T", bound="GetApiAdminScopesResourcesResponse200ResourcesItem")


@_attrs_define
class GetApiAdminScopesResourcesResponse200ResourcesItem:
    """
    Attributes:
        path (str | Unset):
        method (str | Unset):
        required_scopes (list[str] | Unset):
        implied_by (list[str] | Unset):
    """

    path: str | Unset = UNSET
    method: str | Unset = UNSET
    required_scopes: list[str] | Unset = UNSET
    implied_by: list[str] | Unset = UNSET
    additional_properties: dict[str, Any] = _attrs_field(init=False, factory=dict)

    def to_dict(self) -> dict[str, Any]:
        path = self.path

        method = self.method

        required_scopes: list[str] | Unset = UNSET
        if not isinstance(self.required_scopes, Unset):
            required_scopes = self.required_scopes

        implied_by: list[str] | Unset = UNSET
        if not isinstance(self.implied_by, Unset):
            implied_by = self.implied_by

        field_dict: dict[str, Any] = {}
        field_dict.update(self.additional_properties)
        field_dict.update({})
        if path is not UNSET:
            field_dict["path"] = path
        if method is not UNSET:
            field_dict["method"] = method
        if required_scopes is not UNSET:
            field_dict["required_scopes"] = required_scopes
        if implied_by is not UNSET:
            field_dict["implied_by"] = implied_by

        return field_dict

    @classmethod
    def from_dict(cls: type[T], src_dict: Mapping[str, Any]) -> T:
        d = dict(src_dict)
        path = d.pop("path", UNSET)

        method = d.pop("method", UNSET)

        required_scopes = cast(list[str], d.pop("required_scopes", UNSET))

        implied_by = cast(list[str], d.pop("implied_by", UNSET))

        get_api_admin_scopes_resources_response_200_resources_item = cls(
            path=path,
            method=method,
            required_scopes=required_scopes,
            implied_by=implied_by,
        )

        get_api_admin_scopes_resources_response_200_resources_item.additional_properties = d
        return get_api_admin_scopes_resources_response_200_resources_item

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
