from __future__ import annotations

from collections.abc import Mapping
from typing import TYPE_CHECKING, Any, TypeVar

from attrs import define as _attrs_define
from attrs import field as _attrs_field

from ..types import UNSET, Unset

if TYPE_CHECKING:
    from ..models.get_api_admin_scopes_resources_response_200_resources_item import (
        GetApiAdminScopesResourcesResponse200ResourcesItem,
    )


T = TypeVar("T", bound="GetApiAdminScopesResourcesResponse200")


@_attrs_define
class GetApiAdminScopesResourcesResponse200:
    """
    Attributes:
        resources (list[GetApiAdminScopesResourcesResponse200ResourcesItem] | Unset):
    """

    resources: list[GetApiAdminScopesResourcesResponse200ResourcesItem] | Unset = UNSET
    additional_properties: dict[str, Any] = _attrs_field(init=False, factory=dict)

    def to_dict(self) -> dict[str, Any]:
        resources: list[dict[str, Any]] | Unset = UNSET
        if not isinstance(self.resources, Unset):
            resources = []
            for resources_item_data in self.resources:
                resources_item = resources_item_data.to_dict()
                resources.append(resources_item)

        field_dict: dict[str, Any] = {}
        field_dict.update(self.additional_properties)
        field_dict.update({})
        if resources is not UNSET:
            field_dict["resources"] = resources

        return field_dict

    @classmethod
    def from_dict(cls: type[T], src_dict: Mapping[str, Any]) -> T:
        from ..models.get_api_admin_scopes_resources_response_200_resources_item import (
            GetApiAdminScopesResourcesResponse200ResourcesItem,
        )

        d = dict(src_dict)
        _resources = d.pop("resources", UNSET)
        resources: list[GetApiAdminScopesResourcesResponse200ResourcesItem] | Unset = UNSET
        if _resources is not UNSET:
            resources = []
            for resources_item_data in _resources:
                resources_item = GetApiAdminScopesResourcesResponse200ResourcesItem.from_dict(resources_item_data)

                resources.append(resources_item)

        get_api_admin_scopes_resources_response_200 = cls(
            resources=resources,
        )

        get_api_admin_scopes_resources_response_200.additional_properties = d
        return get_api_admin_scopes_resources_response_200

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
