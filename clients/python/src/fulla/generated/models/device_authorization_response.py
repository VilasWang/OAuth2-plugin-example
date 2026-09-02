from __future__ import annotations

from collections.abc import Mapping
from typing import Any, TypeVar

from attrs import define as _attrs_define
from attrs import field as _attrs_field

from ..types import UNSET, Unset

T = TypeVar("T", bound="DeviceAuthorizationResponse")


@_attrs_define
class DeviceAuthorizationResponse:
    """RFC 8628 device authorization response. The verification URIs point at the admin console device-approval page by
    default (admin_console.url config + /admin/devices, #146); override with
    custom_config.device_authorization.verification_uri.

        Attributes:
            device_code (str):
            user_code (str): 8 characters from the A-Z2-9 alphabet (no 0/1/I/O).
            verification_uri (str): Page the user visits to approve the device (admin console /admin/devices by default).
            expires_in (int): Device code lifetime in seconds (600).
            interval (int): Required polling interval in seconds (5).
            verification_uri_complete (str | Unset): verification_uri with ?user_code=<code> appended (RFC 8628 §3.3.1) so
                the approval page can prefill the code.
    """

    device_code: str
    user_code: str
    verification_uri: str
    expires_in: int
    interval: int
    verification_uri_complete: str | Unset = UNSET
    additional_properties: dict[str, Any] = _attrs_field(init=False, factory=dict)

    def to_dict(self) -> dict[str, Any]:
        device_code = self.device_code

        user_code = self.user_code

        verification_uri = self.verification_uri

        expires_in = self.expires_in

        interval = self.interval

        verification_uri_complete = self.verification_uri_complete

        field_dict: dict[str, Any] = {}
        field_dict.update(self.additional_properties)
        field_dict.update(
            {
                "device_code": device_code,
                "user_code": user_code,
                "verification_uri": verification_uri,
                "expires_in": expires_in,
                "interval": interval,
            }
        )
        if verification_uri_complete is not UNSET:
            field_dict["verification_uri_complete"] = verification_uri_complete

        return field_dict

    @classmethod
    def from_dict(cls: type[T], src_dict: Mapping[str, Any]) -> T:
        d = dict(src_dict)
        device_code = d.pop("device_code")

        user_code = d.pop("user_code")

        verification_uri = d.pop("verification_uri")

        expires_in = d.pop("expires_in")

        interval = d.pop("interval")

        verification_uri_complete = d.pop("verification_uri_complete", UNSET)

        device_authorization_response = cls(
            device_code=device_code,
            user_code=user_code,
            verification_uri=verification_uri,
            expires_in=expires_in,
            interval=interval,
            verification_uri_complete=verification_uri_complete,
        )

        device_authorization_response.additional_properties = d
        return device_authorization_response

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
