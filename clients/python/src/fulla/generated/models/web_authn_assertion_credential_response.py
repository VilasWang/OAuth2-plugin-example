from __future__ import annotations

from collections.abc import Mapping
from typing import Any, TypeVar

from attrs import define as _attrs_define
from attrs import field as _attrs_field

T = TypeVar("T", bound="WebAuthnAssertionCredentialResponse")


@_attrs_define
class WebAuthnAssertionCredentialResponse:
    """
    Attributes:
        authenticator_data (str):
        client_data_json (str):
        signature (str): base64url ASN.1 DER ECDSA signature.
    """

    authenticator_data: str
    client_data_json: str
    signature: str
    additional_properties: dict[str, Any] = _attrs_field(init=False, factory=dict)

    def to_dict(self) -> dict[str, Any]:
        authenticator_data = self.authenticator_data

        client_data_json = self.client_data_json

        signature = self.signature

        field_dict: dict[str, Any] = {}
        field_dict.update(self.additional_properties)
        field_dict.update(
            {
                "authenticatorData": authenticator_data,
                "clientDataJSON": client_data_json,
                "signature": signature,
            }
        )

        return field_dict

    @classmethod
    def from_dict(cls: type[T], src_dict: Mapping[str, Any]) -> T:
        d = dict(src_dict)
        authenticator_data = d.pop("authenticatorData")

        client_data_json = d.pop("clientDataJSON")

        signature = d.pop("signature")

        web_authn_assertion_credential_response = cls(
            authenticator_data=authenticator_data,
            client_data_json=client_data_json,
            signature=signature,
        )

        web_authn_assertion_credential_response.additional_properties = d
        return web_authn_assertion_credential_response

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
