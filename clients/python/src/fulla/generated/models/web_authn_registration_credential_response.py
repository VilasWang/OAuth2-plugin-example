from __future__ import annotations

from collections.abc import Mapping
from typing import Any, TypeVar

from attrs import define as _attrs_define
from attrs import field as _attrs_field

T = TypeVar("T", bound="WebAuthnRegistrationCredentialResponse")


@_attrs_define
class WebAuthnRegistrationCredentialResponse:
    """
    Attributes:
        attestation_object (str): base64url CBOR attestation object (fmt "none" only).
        client_data_json (str):
    """

    attestation_object: str
    client_data_json: str
    additional_properties: dict[str, Any] = _attrs_field(init=False, factory=dict)

    def to_dict(self) -> dict[str, Any]:
        attestation_object = self.attestation_object

        client_data_json = self.client_data_json

        field_dict: dict[str, Any] = {}
        field_dict.update(self.additional_properties)
        field_dict.update(
            {
                "attestationObject": attestation_object,
                "clientDataJSON": client_data_json,
            }
        )

        return field_dict

    @classmethod
    def from_dict(cls: type[T], src_dict: Mapping[str, Any]) -> T:
        d = dict(src_dict)
        attestation_object = d.pop("attestationObject")

        client_data_json = d.pop("clientDataJSON")

        web_authn_registration_credential_response = cls(
            attestation_object=attestation_object,
            client_data_json=client_data_json,
        )

        web_authn_registration_credential_response.additional_properties = d
        return web_authn_registration_credential_response

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
