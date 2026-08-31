from __future__ import annotations

from collections.abc import Mapping
from typing import TYPE_CHECKING, Any, TypeVar

from attrs import define as _attrs_define
from attrs import field as _attrs_field

from ..types import UNSET, Unset

if TYPE_CHECKING:
    from ..models.web_authn_assertion_credential_response import WebAuthnAssertionCredentialResponse


T = TypeVar("T", bound="WebAuthnAssertionCredential")


@_attrs_define
class WebAuthnAssertionCredential:
    """Browser PublicKeyCredential for navigator.credentials.get() (#142). All binary fields are base64url without padding.

    Attributes:
        id (str): base64url credential id (must equal rawId).
        raw_id (str):
        response (WebAuthnAssertionCredentialResponse):
        user_handle (str | Unset): Optional base64url user handle; when present it must name the credential's owner.
    """

    id: str
    raw_id: str
    response: WebAuthnAssertionCredentialResponse
    user_handle: str | Unset = UNSET
    additional_properties: dict[str, Any] = _attrs_field(init=False, factory=dict)

    def to_dict(self) -> dict[str, Any]:
        id = self.id

        raw_id = self.raw_id

        response = self.response.to_dict()

        user_handle = self.user_handle

        field_dict: dict[str, Any] = {}
        field_dict.update(self.additional_properties)
        field_dict.update(
            {
                "id": id,
                "rawId": raw_id,
                "response": response,
            }
        )
        if user_handle is not UNSET:
            field_dict["userHandle"] = user_handle

        return field_dict

    @classmethod
    def from_dict(cls: type[T], src_dict: Mapping[str, Any]) -> T:
        from ..models.web_authn_assertion_credential_response import WebAuthnAssertionCredentialResponse

        d = dict(src_dict)
        id = d.pop("id")

        raw_id = d.pop("rawId")

        response = WebAuthnAssertionCredentialResponse.from_dict(d.pop("response"))

        user_handle = d.pop("userHandle", UNSET)

        web_authn_assertion_credential = cls(
            id=id,
            raw_id=raw_id,
            response=response,
            user_handle=user_handle,
        )

        web_authn_assertion_credential.additional_properties = d
        return web_authn_assertion_credential

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
