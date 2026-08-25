from __future__ import annotations

from collections.abc import Mapping
from typing import Any, TypeVar, cast

from attrs import define as _attrs_define
from attrs import field as _attrs_field

from ..types import UNSET, Unset

T = TypeVar("T", bound="OpenIDConfiguration")


@_attrs_define
class OpenIDConfiguration:
    """OIDC Discovery 1.0 provider metadata.

    Attributes:
        issuer (str | Unset):
        authorization_endpoint (str | Unset):
        token_endpoint (str | Unset):
        device_authorization_endpoint (str | Unset):
        userinfo_endpoint (str | Unset):
        jwks_uri (str | Unset):
        introspection_endpoint (str | Unset):
        revocation_endpoint (str | Unset):
        registration_endpoint (str | Unset):
        scopes_supported (list[str] | Unset):
        response_types_supported (list[str] | Unset):
        grant_types_supported (list[str] | Unset):
        subject_types_supported (list[str] | Unset):
        id_token_signing_alg_values_supported (list[str] | Unset):
        token_endpoint_auth_methods_supported (list[str] | Unset):
        code_challenge_methods_supported (list[str] | Unset):
        prompt_values_supported (list[str] | Unset):
        acr_values_supported (list[str] | Unset):
        end_session_endpoint (str | Unset):
        backchannel_logout_supported (bool | Unset):
        backchannel_logout_session_supported (bool | Unset):
        claims_supported (list[str] | Unset):
    """

    issuer: str | Unset = UNSET
    authorization_endpoint: str | Unset = UNSET
    token_endpoint: str | Unset = UNSET
    device_authorization_endpoint: str | Unset = UNSET
    userinfo_endpoint: str | Unset = UNSET
    jwks_uri: str | Unset = UNSET
    introspection_endpoint: str | Unset = UNSET
    revocation_endpoint: str | Unset = UNSET
    registration_endpoint: str | Unset = UNSET
    scopes_supported: list[str] | Unset = UNSET
    response_types_supported: list[str] | Unset = UNSET
    grant_types_supported: list[str] | Unset = UNSET
    subject_types_supported: list[str] | Unset = UNSET
    id_token_signing_alg_values_supported: list[str] | Unset = UNSET
    token_endpoint_auth_methods_supported: list[str] | Unset = UNSET
    code_challenge_methods_supported: list[str] | Unset = UNSET
    prompt_values_supported: list[str] | Unset = UNSET
    acr_values_supported: list[str] | Unset = UNSET
    end_session_endpoint: str | Unset = UNSET
    backchannel_logout_supported: bool | Unset = UNSET
    backchannel_logout_session_supported: bool | Unset = UNSET
    claims_supported: list[str] | Unset = UNSET
    additional_properties: dict[str, Any] = _attrs_field(init=False, factory=dict)

    def to_dict(self) -> dict[str, Any]:
        issuer = self.issuer

        authorization_endpoint = self.authorization_endpoint

        token_endpoint = self.token_endpoint

        device_authorization_endpoint = self.device_authorization_endpoint

        userinfo_endpoint = self.userinfo_endpoint

        jwks_uri = self.jwks_uri

        introspection_endpoint = self.introspection_endpoint

        revocation_endpoint = self.revocation_endpoint

        registration_endpoint = self.registration_endpoint

        scopes_supported: list[str] | Unset = UNSET
        if not isinstance(self.scopes_supported, Unset):
            scopes_supported = self.scopes_supported

        response_types_supported: list[str] | Unset = UNSET
        if not isinstance(self.response_types_supported, Unset):
            response_types_supported = self.response_types_supported

        grant_types_supported: list[str] | Unset = UNSET
        if not isinstance(self.grant_types_supported, Unset):
            grant_types_supported = self.grant_types_supported

        subject_types_supported: list[str] | Unset = UNSET
        if not isinstance(self.subject_types_supported, Unset):
            subject_types_supported = self.subject_types_supported

        id_token_signing_alg_values_supported: list[str] | Unset = UNSET
        if not isinstance(self.id_token_signing_alg_values_supported, Unset):
            id_token_signing_alg_values_supported = self.id_token_signing_alg_values_supported

        token_endpoint_auth_methods_supported: list[str] | Unset = UNSET
        if not isinstance(self.token_endpoint_auth_methods_supported, Unset):
            token_endpoint_auth_methods_supported = self.token_endpoint_auth_methods_supported

        code_challenge_methods_supported: list[str] | Unset = UNSET
        if not isinstance(self.code_challenge_methods_supported, Unset):
            code_challenge_methods_supported = self.code_challenge_methods_supported

        prompt_values_supported: list[str] | Unset = UNSET
        if not isinstance(self.prompt_values_supported, Unset):
            prompt_values_supported = self.prompt_values_supported

        acr_values_supported: list[str] | Unset = UNSET
        if not isinstance(self.acr_values_supported, Unset):
            acr_values_supported = self.acr_values_supported

        end_session_endpoint = self.end_session_endpoint

        backchannel_logout_supported = self.backchannel_logout_supported

        backchannel_logout_session_supported = self.backchannel_logout_session_supported

        claims_supported: list[str] | Unset = UNSET
        if not isinstance(self.claims_supported, Unset):
            claims_supported = self.claims_supported

        field_dict: dict[str, Any] = {}
        field_dict.update(self.additional_properties)
        field_dict.update({})
        if issuer is not UNSET:
            field_dict["issuer"] = issuer
        if authorization_endpoint is not UNSET:
            field_dict["authorization_endpoint"] = authorization_endpoint
        if token_endpoint is not UNSET:
            field_dict["token_endpoint"] = token_endpoint
        if device_authorization_endpoint is not UNSET:
            field_dict["device_authorization_endpoint"] = device_authorization_endpoint
        if userinfo_endpoint is not UNSET:
            field_dict["userinfo_endpoint"] = userinfo_endpoint
        if jwks_uri is not UNSET:
            field_dict["jwks_uri"] = jwks_uri
        if introspection_endpoint is not UNSET:
            field_dict["introspection_endpoint"] = introspection_endpoint
        if revocation_endpoint is not UNSET:
            field_dict["revocation_endpoint"] = revocation_endpoint
        if registration_endpoint is not UNSET:
            field_dict["registration_endpoint"] = registration_endpoint
        if scopes_supported is not UNSET:
            field_dict["scopes_supported"] = scopes_supported
        if response_types_supported is not UNSET:
            field_dict["response_types_supported"] = response_types_supported
        if grant_types_supported is not UNSET:
            field_dict["grant_types_supported"] = grant_types_supported
        if subject_types_supported is not UNSET:
            field_dict["subject_types_supported"] = subject_types_supported
        if id_token_signing_alg_values_supported is not UNSET:
            field_dict["id_token_signing_alg_values_supported"] = id_token_signing_alg_values_supported
        if token_endpoint_auth_methods_supported is not UNSET:
            field_dict["token_endpoint_auth_methods_supported"] = token_endpoint_auth_methods_supported
        if code_challenge_methods_supported is not UNSET:
            field_dict["code_challenge_methods_supported"] = code_challenge_methods_supported
        if prompt_values_supported is not UNSET:
            field_dict["prompt_values_supported"] = prompt_values_supported
        if acr_values_supported is not UNSET:
            field_dict["acr_values_supported"] = acr_values_supported
        if end_session_endpoint is not UNSET:
            field_dict["end_session_endpoint"] = end_session_endpoint
        if backchannel_logout_supported is not UNSET:
            field_dict["backchannel_logout_supported"] = backchannel_logout_supported
        if backchannel_logout_session_supported is not UNSET:
            field_dict["backchannel_logout_session_supported"] = backchannel_logout_session_supported
        if claims_supported is not UNSET:
            field_dict["claims_supported"] = claims_supported

        return field_dict

    @classmethod
    def from_dict(cls: type[T], src_dict: Mapping[str, Any]) -> T:
        d = dict(src_dict)
        issuer = d.pop("issuer", UNSET)

        authorization_endpoint = d.pop("authorization_endpoint", UNSET)

        token_endpoint = d.pop("token_endpoint", UNSET)

        device_authorization_endpoint = d.pop("device_authorization_endpoint", UNSET)

        userinfo_endpoint = d.pop("userinfo_endpoint", UNSET)

        jwks_uri = d.pop("jwks_uri", UNSET)

        introspection_endpoint = d.pop("introspection_endpoint", UNSET)

        revocation_endpoint = d.pop("revocation_endpoint", UNSET)

        registration_endpoint = d.pop("registration_endpoint", UNSET)

        scopes_supported = cast(list[str], d.pop("scopes_supported", UNSET))

        response_types_supported = cast(list[str], d.pop("response_types_supported", UNSET))

        grant_types_supported = cast(list[str], d.pop("grant_types_supported", UNSET))

        subject_types_supported = cast(list[str], d.pop("subject_types_supported", UNSET))

        id_token_signing_alg_values_supported = cast(list[str], d.pop("id_token_signing_alg_values_supported", UNSET))

        token_endpoint_auth_methods_supported = cast(list[str], d.pop("token_endpoint_auth_methods_supported", UNSET))

        code_challenge_methods_supported = cast(list[str], d.pop("code_challenge_methods_supported", UNSET))

        prompt_values_supported = cast(list[str], d.pop("prompt_values_supported", UNSET))

        acr_values_supported = cast(list[str], d.pop("acr_values_supported", UNSET))

        end_session_endpoint = d.pop("end_session_endpoint", UNSET)

        backchannel_logout_supported = d.pop("backchannel_logout_supported", UNSET)

        backchannel_logout_session_supported = d.pop("backchannel_logout_session_supported", UNSET)

        claims_supported = cast(list[str], d.pop("claims_supported", UNSET))

        open_id_configuration = cls(
            issuer=issuer,
            authorization_endpoint=authorization_endpoint,
            token_endpoint=token_endpoint,
            device_authorization_endpoint=device_authorization_endpoint,
            userinfo_endpoint=userinfo_endpoint,
            jwks_uri=jwks_uri,
            introspection_endpoint=introspection_endpoint,
            revocation_endpoint=revocation_endpoint,
            registration_endpoint=registration_endpoint,
            scopes_supported=scopes_supported,
            response_types_supported=response_types_supported,
            grant_types_supported=grant_types_supported,
            subject_types_supported=subject_types_supported,
            id_token_signing_alg_values_supported=id_token_signing_alg_values_supported,
            token_endpoint_auth_methods_supported=token_endpoint_auth_methods_supported,
            code_challenge_methods_supported=code_challenge_methods_supported,
            prompt_values_supported=prompt_values_supported,
            acr_values_supported=acr_values_supported,
            end_session_endpoint=end_session_endpoint,
            backchannel_logout_supported=backchannel_logout_supported,
            backchannel_logout_session_supported=backchannel_logout_session_supported,
            claims_supported=claims_supported,
        )

        open_id_configuration.additional_properties = d
        return open_id_configuration

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
