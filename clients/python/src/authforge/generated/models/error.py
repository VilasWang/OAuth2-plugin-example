from __future__ import annotations

from collections.abc import Mapping
from typing import Any, TypeVar

from attrs import define as _attrs_define
from attrs import field as _attrs_field

from ..types import UNSET, Unset

T = TypeVar("T", bound="Error")


@_attrs_define
class Error:
    """The nested error object of the application error envelope. Application (non-OAuth2-protocol) endpoints respond with
    {"error": {...}} on failure; /oauth2/* and /.well-known/* endpoints use RFC 6749 §5.2 bodies instead (see
    OAuth2Error).

        Attributes:
            category (str | Unset): Error category, e.g. VALIDATION / AUTH / AUTHZ / DB / INTERNAL.
            code (str | Unset): Machine-readable error code, e.g. VALIDATION_INVALID_INPUT.
            message (str | Unset):
            request_id (str | Unset):
            numeric_code (int | Unset): Numeric catalog code (present iff the code is registered, e.g. 3001).
            details (str | Unset): Extra context; only emitted when the server runs in non-production mode.
    """

    category: str | Unset = UNSET
    code: str | Unset = UNSET
    message: str | Unset = UNSET
    request_id: str | Unset = UNSET
    numeric_code: int | Unset = UNSET
    details: str | Unset = UNSET
    additional_properties: dict[str, Any] = _attrs_field(init=False, factory=dict)

    def to_dict(self) -> dict[str, Any]:
        category = self.category

        code = self.code

        message = self.message

        request_id = self.request_id

        numeric_code = self.numeric_code

        details = self.details

        field_dict: dict[str, Any] = {}
        field_dict.update(self.additional_properties)
        field_dict.update({})
        if category is not UNSET:
            field_dict["category"] = category
        if code is not UNSET:
            field_dict["code"] = code
        if message is not UNSET:
            field_dict["message"] = message
        if request_id is not UNSET:
            field_dict["request_id"] = request_id
        if numeric_code is not UNSET:
            field_dict["numeric_code"] = numeric_code
        if details is not UNSET:
            field_dict["details"] = details

        return field_dict

    @classmethod
    def from_dict(cls: type[T], src_dict: Mapping[str, Any]) -> T:
        d = dict(src_dict)
        category = d.pop("category", UNSET)

        code = d.pop("code", UNSET)

        message = d.pop("message", UNSET)

        request_id = d.pop("request_id", UNSET)

        numeric_code = d.pop("numeric_code", UNSET)

        details = d.pop("details", UNSET)

        error = cls(
            category=category,
            code=code,
            message=message,
            request_id=request_id,
            numeric_code=numeric_code,
            details=details,
        )

        error.additional_properties = d
        return error

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
