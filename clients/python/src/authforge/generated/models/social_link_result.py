from __future__ import annotations

from collections.abc import Mapping
from typing import Any, TypeVar

from attrs import define as _attrs_define
from attrs import field as _attrs_field

from ..models.social_link_result_provider import SocialLinkResultProvider
from ..types import UNSET, Unset

T = TypeVar("T", bound="SocialLinkResult")


@_attrs_define
class SocialLinkResult:
    """Result of a link/unlink mutation.

    Attributes:
        provider (SocialLinkResultProvider):
        message (str):
        subject (str | Unset): Provider subject (link success; unlink success keeps the removed subject).
    """

    provider: SocialLinkResultProvider
    message: str
    subject: str | Unset = UNSET
    additional_properties: dict[str, Any] = _attrs_field(init=False, factory=dict)

    def to_dict(self) -> dict[str, Any]:
        provider = self.provider.value

        message = self.message

        subject = self.subject

        field_dict: dict[str, Any] = {}
        field_dict.update(self.additional_properties)
        field_dict.update(
            {
                "provider": provider,
                "message": message,
            }
        )
        if subject is not UNSET:
            field_dict["subject"] = subject

        return field_dict

    @classmethod
    def from_dict(cls: type[T], src_dict: Mapping[str, Any]) -> T:
        d = dict(src_dict)
        provider = SocialLinkResultProvider(d.pop("provider"))

        message = d.pop("message")

        subject = d.pop("subject", UNSET)

        social_link_result = cls(
            provider=provider,
            message=message,
            subject=subject,
        )

        social_link_result.additional_properties = d
        return social_link_result

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
