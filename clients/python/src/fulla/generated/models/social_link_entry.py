from __future__ import annotations

from collections.abc import Mapping
from typing import Any, TypeVar

from attrs import define as _attrs_define
from attrs import field as _attrs_field

from ..models.social_link_entry_provider import SocialLinkEntryProvider
from ..types import UNSET, Unset

T = TypeVar("T", bound="SocialLinkEntry")


@_attrs_define
class SocialLinkEntry:
    """One social provider identity linked to the current user.

    Attributes:
        provider (SocialLinkEntryProvider):
        subject (str): Provider-scoped stable id (GitHub numeric id, Google sub, WeChat openid).
        linked_at (str | Unset): Mapping row creation timestamp.
    """

    provider: SocialLinkEntryProvider
    subject: str
    linked_at: str | Unset = UNSET
    additional_properties: dict[str, Any] = _attrs_field(init=False, factory=dict)

    def to_dict(self) -> dict[str, Any]:
        provider = self.provider.value

        subject = self.subject

        linked_at = self.linked_at

        field_dict: dict[str, Any] = {}
        field_dict.update(self.additional_properties)
        field_dict.update(
            {
                "provider": provider,
                "subject": subject,
            }
        )
        if linked_at is not UNSET:
            field_dict["linked_at"] = linked_at

        return field_dict

    @classmethod
    def from_dict(cls: type[T], src_dict: Mapping[str, Any]) -> T:
        d = dict(src_dict)
        provider = SocialLinkEntryProvider(d.pop("provider"))

        subject = d.pop("subject")

        linked_at = d.pop("linked_at", UNSET)

        social_link_entry = cls(
            provider=provider,
            subject=subject,
            linked_at=linked_at,
        )

        social_link_entry.additional_properties = d
        return social_link_entry

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
