from __future__ import annotations

from collections.abc import Mapping
from typing import TYPE_CHECKING, Any, TypeVar

from attrs import define as _attrs_define
from attrs import field as _attrs_field

if TYPE_CHECKING:
    from ..models.social_link_entry import SocialLinkEntry


T = TypeVar("T", bound="SocialLinksList")


@_attrs_define
class SocialLinksList:
    """
    Attributes:
        social_links (list[SocialLinkEntry]):
        total (int):
    """

    social_links: list[SocialLinkEntry]
    total: int
    additional_properties: dict[str, Any] = _attrs_field(init=False, factory=dict)

    def to_dict(self) -> dict[str, Any]:
        social_links = []
        for social_links_item_data in self.social_links:
            social_links_item = social_links_item_data.to_dict()
            social_links.append(social_links_item)

        total = self.total

        field_dict: dict[str, Any] = {}
        field_dict.update(self.additional_properties)
        field_dict.update(
            {
                "social_links": social_links,
                "total": total,
            }
        )

        return field_dict

    @classmethod
    def from_dict(cls: type[T], src_dict: Mapping[str, Any]) -> T:
        from ..models.social_link_entry import SocialLinkEntry

        d = dict(src_dict)
        social_links = []
        _social_links = d.pop("social_links")
        for social_links_item_data in _social_links:
            social_links_item = SocialLinkEntry.from_dict(social_links_item_data)

            social_links.append(social_links_item)

        total = d.pop("total")

        social_links_list = cls(
            social_links=social_links,
            total=total,
        )

        social_links_list.additional_properties = d
        return social_links_list

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
