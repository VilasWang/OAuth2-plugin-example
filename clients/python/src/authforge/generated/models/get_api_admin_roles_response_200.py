from __future__ import annotations

from collections.abc import Mapping
from typing import TYPE_CHECKING, Any, TypeVar

from attrs import define as _attrs_define
from attrs import field as _attrs_field

from ..types import UNSET, Unset

if TYPE_CHECKING:
    from ..models.get_api_admin_roles_response_200_roles_item import GetApiAdminRolesResponse200RolesItem


T = TypeVar("T", bound="GetApiAdminRolesResponse200")


@_attrs_define
class GetApiAdminRolesResponse200:
    """
    Attributes:
        roles (list[GetApiAdminRolesResponse200RolesItem] | Unset):
        status (str | Unset):
        total (int | Unset):
    """

    roles: list[GetApiAdminRolesResponse200RolesItem] | Unset = UNSET
    status: str | Unset = UNSET
    total: int | Unset = UNSET
    additional_properties: dict[str, Any] = _attrs_field(init=False, factory=dict)

    def to_dict(self) -> dict[str, Any]:
        roles: list[dict[str, Any]] | Unset = UNSET
        if not isinstance(self.roles, Unset):
            roles = []
            for roles_item_data in self.roles:
                roles_item = roles_item_data.to_dict()
                roles.append(roles_item)

        status = self.status

        total = self.total

        field_dict: dict[str, Any] = {}
        field_dict.update(self.additional_properties)
        field_dict.update({})
        if roles is not UNSET:
            field_dict["roles"] = roles
        if status is not UNSET:
            field_dict["status"] = status
        if total is not UNSET:
            field_dict["total"] = total

        return field_dict

    @classmethod
    def from_dict(cls: type[T], src_dict: Mapping[str, Any]) -> T:
        from ..models.get_api_admin_roles_response_200_roles_item import GetApiAdminRolesResponse200RolesItem

        d = dict(src_dict)
        _roles = d.pop("roles", UNSET)
        roles: list[GetApiAdminRolesResponse200RolesItem] | Unset = UNSET
        if _roles is not UNSET:
            roles = []
            for roles_item_data in _roles:
                roles_item = GetApiAdminRolesResponse200RolesItem.from_dict(roles_item_data)

                roles.append(roles_item)

        status = d.pop("status", UNSET)

        total = d.pop("total", UNSET)

        get_api_admin_roles_response_200 = cls(
            roles=roles,
            status=status,
            total=total,
        )

        get_api_admin_roles_response_200.additional_properties = d
        return get_api_admin_roles_response_200

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
