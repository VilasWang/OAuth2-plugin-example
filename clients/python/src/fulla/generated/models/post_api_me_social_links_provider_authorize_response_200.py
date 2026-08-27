from __future__ import annotations

from collections.abc import Mapping
from typing import Any, TypeVar

from attrs import define as _attrs_define
from attrs import field as _attrs_field

from ..types import UNSET, Unset

T = TypeVar("T", bound="PostApiMeSocialLinksProviderAuthorizeResponse200")


@_attrs_define
class PostApiMeSocialLinksProviderAuthorizeResponse200:
    """
    Attributes:
        provider (str | Unset):
        state (str | Unset):
        authorize_url (str | Unset):
    """

    provider: str | Unset = UNSET
    state: str | Unset = UNSET
    authorize_url: str | Unset = UNSET
    additional_properties: dict[str, Any] = _attrs_field(init=False, factory=dict)

    def to_dict(self) -> dict[str, Any]:
        provider = self.provider

        state = self.state

        authorize_url = self.authorize_url

        field_dict: dict[str, Any] = {}
        field_dict.update(self.additional_properties)
        field_dict.update({})
        if provider is not UNSET:
            field_dict["provider"] = provider
        if state is not UNSET:
            field_dict["state"] = state
        if authorize_url is not UNSET:
            field_dict["authorize_url"] = authorize_url

        return field_dict

    @classmethod
    def from_dict(cls: type[T], src_dict: Mapping[str, Any]) -> T:
        d = dict(src_dict)
        provider = d.pop("provider", UNSET)

        state = d.pop("state", UNSET)

        authorize_url = d.pop("authorize_url", UNSET)

        post_api_me_social_links_provider_authorize_response_200 = cls(
            provider=provider,
            state=state,
            authorize_url=authorize_url,
        )

        post_api_me_social_links_provider_authorize_response_200.additional_properties = d
        return post_api_me_social_links_provider_authorize_response_200

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
