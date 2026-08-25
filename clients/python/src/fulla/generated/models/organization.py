from __future__ import annotations

from collections.abc import Mapping
from typing import Any, TypeVar

from attrs import define as _attrs_define
from attrs import field as _attrs_field

from ..types import UNSET, Unset

T = TypeVar("T", bound="Organization")


@_attrs_define
class Organization:
    """
    Attributes:
        id (int | Unset):
        slug (str | Unset):
        name (str | Unset):
        logo_uri (str | Unset):
        primary_color (str | Unset):
        issuer_override (str | Unset):
    """

    id: int | Unset = UNSET
    slug: str | Unset = UNSET
    name: str | Unset = UNSET
    logo_uri: str | Unset = UNSET
    primary_color: str | Unset = UNSET
    issuer_override: str | Unset = UNSET
    additional_properties: dict[str, Any] = _attrs_field(init=False, factory=dict)

    def to_dict(self) -> dict[str, Any]:
        id = self.id

        slug = self.slug

        name = self.name

        logo_uri = self.logo_uri

        primary_color = self.primary_color

        issuer_override = self.issuer_override

        field_dict: dict[str, Any] = {}
        field_dict.update(self.additional_properties)
        field_dict.update({})
        if id is not UNSET:
            field_dict["id"] = id
        if slug is not UNSET:
            field_dict["slug"] = slug
        if name is not UNSET:
            field_dict["name"] = name
        if logo_uri is not UNSET:
            field_dict["logo_uri"] = logo_uri
        if primary_color is not UNSET:
            field_dict["primary_color"] = primary_color
        if issuer_override is not UNSET:
            field_dict["issuer_override"] = issuer_override

        return field_dict

    @classmethod
    def from_dict(cls: type[T], src_dict: Mapping[str, Any]) -> T:
        d = dict(src_dict)
        id = d.pop("id", UNSET)

        slug = d.pop("slug", UNSET)

        name = d.pop("name", UNSET)

        logo_uri = d.pop("logo_uri", UNSET)

        primary_color = d.pop("primary_color", UNSET)

        issuer_override = d.pop("issuer_override", UNSET)

        organization = cls(
            id=id,
            slug=slug,
            name=name,
            logo_uri=logo_uri,
            primary_color=primary_color,
            issuer_override=issuer_override,
        )

        organization.additional_properties = d
        return organization

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
