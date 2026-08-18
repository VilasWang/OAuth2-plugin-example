from __future__ import annotations

from collections.abc import Mapping
from typing import Any, TypeVar

from attrs import define as _attrs_define
from attrs import field as _attrs_field

from ..types import UNSET, Unset

T = TypeVar("T", bound="PostApiGithubLoginResponse200")


@_attrs_define
class PostApiGithubLoginResponse200:
    """
    Attributes:
        avatar_url (str | Unset):
        email (str | Unset):
        id (int | Unset):
        login (str | Unset):
        name (str | Unset):
    """

    avatar_url: str | Unset = UNSET
    email: str | Unset = UNSET
    id: int | Unset = UNSET
    login: str | Unset = UNSET
    name: str | Unset = UNSET
    additional_properties: dict[str, Any] = _attrs_field(init=False, factory=dict)

    def to_dict(self) -> dict[str, Any]:
        avatar_url = self.avatar_url

        email = self.email

        id = self.id

        login = self.login

        name = self.name

        field_dict: dict[str, Any] = {}
        field_dict.update(self.additional_properties)
        field_dict.update({})
        if avatar_url is not UNSET:
            field_dict["avatar_url"] = avatar_url
        if email is not UNSET:
            field_dict["email"] = email
        if id is not UNSET:
            field_dict["id"] = id
        if login is not UNSET:
            field_dict["login"] = login
        if name is not UNSET:
            field_dict["name"] = name

        return field_dict

    @classmethod
    def from_dict(cls: type[T], src_dict: Mapping[str, Any]) -> T:
        d = dict(src_dict)
        avatar_url = d.pop("avatar_url", UNSET)

        email = d.pop("email", UNSET)

        id = d.pop("id", UNSET)

        login = d.pop("login", UNSET)

        name = d.pop("name", UNSET)

        post_api_github_login_response_200 = cls(
            avatar_url=avatar_url,
            email=email,
            id=id,
            login=login,
            name=name,
        )

        post_api_github_login_response_200.additional_properties = d
        return post_api_github_login_response_200

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
