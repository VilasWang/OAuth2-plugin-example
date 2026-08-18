from __future__ import annotations

from collections.abc import Mapping
from typing import Any, TypeVar

from attrs import define as _attrs_define
from attrs import field as _attrs_field

from ..types import UNSET, Unset

T = TypeVar("T", bound="GetApiAdminScopesResponse200ScopesItem")


@_attrs_define
class GetApiAdminScopesResponse200ScopesItem:
    """
    Attributes:
        description (str | Unset):
        id (int | Unset):
        is_default (bool | Unset):
        mapped_role (str | Unset):
        name (str | Unset):
        requires_admin_role (bool | Unset):
    """

    description: str | Unset = UNSET
    id: int | Unset = UNSET
    is_default: bool | Unset = UNSET
    mapped_role: str | Unset = UNSET
    name: str | Unset = UNSET
    requires_admin_role: bool | Unset = UNSET
    additional_properties: dict[str, Any] = _attrs_field(init=False, factory=dict)

    def to_dict(self) -> dict[str, Any]:
        description = self.description

        id = self.id

        is_default = self.is_default

        mapped_role = self.mapped_role

        name = self.name

        requires_admin_role = self.requires_admin_role

        field_dict: dict[str, Any] = {}
        field_dict.update(self.additional_properties)
        field_dict.update({})
        if description is not UNSET:
            field_dict["description"] = description
        if id is not UNSET:
            field_dict["id"] = id
        if is_default is not UNSET:
            field_dict["is_default"] = is_default
        if mapped_role is not UNSET:
            field_dict["mapped_role"] = mapped_role
        if name is not UNSET:
            field_dict["name"] = name
        if requires_admin_role is not UNSET:
            field_dict["requires_admin_role"] = requires_admin_role

        return field_dict

    @classmethod
    def from_dict(cls: type[T], src_dict: Mapping[str, Any]) -> T:
        d = dict(src_dict)
        description = d.pop("description", UNSET)

        id = d.pop("id", UNSET)

        is_default = d.pop("is_default", UNSET)

        mapped_role = d.pop("mapped_role", UNSET)

        name = d.pop("name", UNSET)

        requires_admin_role = d.pop("requires_admin_role", UNSET)

        get_api_admin_scopes_response_200_scopes_item = cls(
            description=description,
            id=id,
            is_default=is_default,
            mapped_role=mapped_role,
            name=name,
            requires_admin_role=requires_admin_role,
        )

        get_api_admin_scopes_response_200_scopes_item.additional_properties = d
        return get_api_admin_scopes_response_200_scopes_item

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
