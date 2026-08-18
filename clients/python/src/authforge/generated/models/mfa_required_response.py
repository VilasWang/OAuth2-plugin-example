from __future__ import annotations

from collections.abc import Mapping
from typing import Any, TypeVar

from attrs import define as _attrs_define
from attrs import field as _attrs_field

from ..types import UNSET, Unset

T = TypeVar("T", bound="MfaRequiredResponse")


@_attrs_define
class MfaRequiredResponse:
    """/oauth2/login response when the account has MFA enabled; complete the flow at POST /oauth2/mfa/verify.

    Attributes:
        mfa_required (bool):
        mfa_token (str): Pending-MFA token to submit to /oauth2/mfa/verify.
        message (str | Unset):
    """

    mfa_required: bool
    mfa_token: str
    message: str | Unset = UNSET
    additional_properties: dict[str, Any] = _attrs_field(init=False, factory=dict)

    def to_dict(self) -> dict[str, Any]:
        mfa_required = self.mfa_required

        mfa_token = self.mfa_token

        message = self.message

        field_dict: dict[str, Any] = {}
        field_dict.update(self.additional_properties)
        field_dict.update(
            {
                "mfa_required": mfa_required,
                "mfa_token": mfa_token,
            }
        )
        if message is not UNSET:
            field_dict["message"] = message

        return field_dict

    @classmethod
    def from_dict(cls: type[T], src_dict: Mapping[str, Any]) -> T:
        d = dict(src_dict)
        mfa_required = d.pop("mfa_required")

        mfa_token = d.pop("mfa_token")

        message = d.pop("message", UNSET)

        mfa_required_response = cls(
            mfa_required=mfa_required,
            mfa_token=mfa_token,
            message=message,
        )

        mfa_required_response.additional_properties = d
        return mfa_required_response

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
