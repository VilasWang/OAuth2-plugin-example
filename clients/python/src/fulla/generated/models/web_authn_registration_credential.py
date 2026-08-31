from __future__ import annotations

from collections.abc import Mapping
from typing import TYPE_CHECKING, Any, TypeVar

from attrs import define as _attrs_define
from attrs import field as _attrs_field

from ..types import UNSET, Unset

if TYPE_CHECKING:
    from ..models.web_authn_registration_credential_response import WebAuthnRegistrationCredentialResponse


T = TypeVar("T", bound="WebAuthnRegistrationCredential")


@_attrs_define
class WebAuthnRegistrationCredential:
    """Browser PublicKeyCredential for navigator.credentials.create() (#142). All binary fields are base64url without
    padding. The server ignores any client-submitted public_key: the stored key comes from the verified attestation
    object's COSE bytes.

        Attributes:
            id (str): base64url credential id (must equal rawId).
            raw_id (str):
            response (WebAuthnRegistrationCredentialResponse):
            name (str | Unset): Optional human-readable label (default "Passkey").
    """

    id: str
    raw_id: str
    response: WebAuthnRegistrationCredentialResponse
    name: str | Unset = UNSET
    additional_properties: dict[str, Any] = _attrs_field(init=False, factory=dict)

    def to_dict(self) -> dict[str, Any]:
        id = self.id

        raw_id = self.raw_id

        response = self.response.to_dict()

        name = self.name

        field_dict: dict[str, Any] = {}
        field_dict.update(self.additional_properties)
        field_dict.update(
            {
                "id": id,
                "rawId": raw_id,
                "response": response,
            }
        )
        if name is not UNSET:
            field_dict["name"] = name

        return field_dict

    @classmethod
    def from_dict(cls: type[T], src_dict: Mapping[str, Any]) -> T:
        from ..models.web_authn_registration_credential_response import WebAuthnRegistrationCredentialResponse

        d = dict(src_dict)
        id = d.pop("id")

        raw_id = d.pop("rawId")

        response = WebAuthnRegistrationCredentialResponse.from_dict(d.pop("response"))

        name = d.pop("name", UNSET)

        web_authn_registration_credential = cls(
            id=id,
            raw_id=raw_id,
            response=response,
            name=name,
        )

        web_authn_registration_credential.additional_properties = d
        return web_authn_registration_credential

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
