from __future__ import annotations

from collections.abc import Mapping
from typing import Any, TypeVar

from attrs import define as _attrs_define
from attrs import field as _attrs_field

from ..types import UNSET, Unset

T = TypeVar("T", bound="MfaVerifyRequest")


@_attrs_define
class MfaVerifyRequest:
    """/oauth2/mfa/verify completion payload (JSON body or form-encoded).

    Attributes:
        mfa_token (str): The MFA pending token returned by /oauth2/login when mfa_required was true.
        code (str): The 6-digit TOTP code from the user's authenticator.
        client_id (str): Must match the first-factor login.
        redirect_uri (str): Must match the first-factor login.
        scope (str | Unset): Defaults to "openid profile email".
        nonce (str | Unset):
        code_verifier (str | Unset): PKCE code_verifier matching the code_challenge sent on the first-factor
            /oauth2/login step. Required for PUBLIC clients.
    """

    mfa_token: str
    code: str
    client_id: str
    redirect_uri: str
    scope: str | Unset = UNSET
    nonce: str | Unset = UNSET
    code_verifier: str | Unset = UNSET
    additional_properties: dict[str, Any] = _attrs_field(init=False, factory=dict)

    def to_dict(self) -> dict[str, Any]:
        mfa_token = self.mfa_token

        code = self.code

        client_id = self.client_id

        redirect_uri = self.redirect_uri

        scope = self.scope

        nonce = self.nonce

        code_verifier = self.code_verifier

        field_dict: dict[str, Any] = {}
        field_dict.update(self.additional_properties)
        field_dict.update(
            {
                "mfa_token": mfa_token,
                "code": code,
                "client_id": client_id,
                "redirect_uri": redirect_uri,
            }
        )
        if scope is not UNSET:
            field_dict["scope"] = scope
        if nonce is not UNSET:
            field_dict["nonce"] = nonce
        if code_verifier is not UNSET:
            field_dict["code_verifier"] = code_verifier

        return field_dict

    @classmethod
    def from_dict(cls: type[T], src_dict: Mapping[str, Any]) -> T:
        d = dict(src_dict)
        mfa_token = d.pop("mfa_token")

        code = d.pop("code")

        client_id = d.pop("client_id")

        redirect_uri = d.pop("redirect_uri")

        scope = d.pop("scope", UNSET)

        nonce = d.pop("nonce", UNSET)

        code_verifier = d.pop("code_verifier", UNSET)

        mfa_verify_request = cls(
            mfa_token=mfa_token,
            code=code,
            client_id=client_id,
            redirect_uri=redirect_uri,
            scope=scope,
            nonce=nonce,
            code_verifier=code_verifier,
        )

        mfa_verify_request.additional_properties = d
        return mfa_verify_request

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
