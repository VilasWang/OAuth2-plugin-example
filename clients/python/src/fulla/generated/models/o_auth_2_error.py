from __future__ import annotations

from collections.abc import Mapping
from typing import Any, TypeVar

from attrs import define as _attrs_define
from attrs import field as _attrs_field

from ..models.o_auth_2_error_error import OAuth2ErrorError
from ..types import UNSET, Unset

T = TypeVar("T", bound="OAuth2Error")


@_attrs_define
class OAuth2Error:
    """RFC 6749 §5.2 error body used by /oauth2/* and /.well-known/* endpoints (with Cache-Control: no-store). HTTP status
    per code: invalid_request 400, invalid_client 401, invalid_grant 400, unauthorized_client 400,
    unsupported_grant_type 400, invalid_scope 400, access_denied 403, authorization_pending 400, slow_down 400,
    expired_token 400, server_error 500, temporarily_unavailable 503. Bearer-protected resources (userinfo) additionally
    use the RFC 6750 section 3.1 codes invalid_token (401) and insufficient_scope (403).

        Attributes:
            error (OAuth2ErrorError):
            error_description (str | Unset):
            error_uri (str | Unset):
    """

    error: OAuth2ErrorError
    error_description: str | Unset = UNSET
    error_uri: str | Unset = UNSET
    additional_properties: dict[str, Any] = _attrs_field(init=False, factory=dict)

    def to_dict(self) -> dict[str, Any]:
        error = self.error.value

        error_description = self.error_description

        error_uri = self.error_uri

        field_dict: dict[str, Any] = {}
        field_dict.update(self.additional_properties)
        field_dict.update(
            {
                "error": error,
            }
        )
        if error_description is not UNSET:
            field_dict["error_description"] = error_description
        if error_uri is not UNSET:
            field_dict["error_uri"] = error_uri

        return field_dict

    @classmethod
    def from_dict(cls: type[T], src_dict: Mapping[str, Any]) -> T:
        d = dict(src_dict)
        error = OAuth2ErrorError(d.pop("error"))

        error_description = d.pop("error_description", UNSET)

        error_uri = d.pop("error_uri", UNSET)

        o_auth_2_error = cls(
            error=error,
            error_description=error_description,
            error_uri=error_uri,
        )

        o_auth_2_error.additional_properties = d
        return o_auth_2_error

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
