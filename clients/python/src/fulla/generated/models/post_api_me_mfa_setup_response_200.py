from __future__ import annotations

from collections.abc import Mapping
from typing import Any, TypeVar

from attrs import define as _attrs_define
from attrs import field as _attrs_field

from ..types import UNSET, Unset

T = TypeVar("T", bound="PostApiMeMfaSetupResponse200")


@_attrs_define
class PostApiMeMfaSetupResponse200:
    """
    Attributes:
        secret (str): Base32 TOTP shared secret.
        otpauth_uri (str): otpauth:// provisioning URI (encode as QR for authenticators).
        message (str | Unset):
    """

    secret: str
    otpauth_uri: str
    message: str | Unset = UNSET
    additional_properties: dict[str, Any] = _attrs_field(init=False, factory=dict)

    def to_dict(self) -> dict[str, Any]:
        secret = self.secret

        otpauth_uri = self.otpauth_uri

        message = self.message

        field_dict: dict[str, Any] = {}
        field_dict.update(self.additional_properties)
        field_dict.update(
            {
                "secret": secret,
                "otpauth_uri": otpauth_uri,
            }
        )
        if message is not UNSET:
            field_dict["message"] = message

        return field_dict

    @classmethod
    def from_dict(cls: type[T], src_dict: Mapping[str, Any]) -> T:
        d = dict(src_dict)
        secret = d.pop("secret")

        otpauth_uri = d.pop("otpauth_uri")

        message = d.pop("message", UNSET)

        post_api_me_mfa_setup_response_200 = cls(
            secret=secret,
            otpauth_uri=otpauth_uri,
            message=message,
        )

        post_api_me_mfa_setup_response_200.additional_properties = d
        return post_api_me_mfa_setup_response_200

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
