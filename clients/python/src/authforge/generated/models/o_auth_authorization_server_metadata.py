from __future__ import annotations

from collections.abc import Mapping
from typing import Any, TypeVar, cast

from attrs import define as _attrs_define
from attrs import field as _attrs_field

from ..types import UNSET, Unset

T = TypeVar("T", bound="OAuthAuthorizationServerMetadata")


@_attrs_define
class OAuthAuthorizationServerMetadata:
    """RFC 8414 authorization server metadata (no userinfo/jwks entries; OIDC discovery carries those).

    Attributes:
        issuer (str | Unset):
        authorization_endpoint (str | Unset):
        token_endpoint (str | Unset):
        device_authorization_endpoint (str | Unset):
        introspection_endpoint (str | Unset):
        introspection_endpoint_auth_methods_supported (list[str] | Unset):
        revocation_endpoint (str | Unset):
        revocation_endpoint_auth_methods_supported (list[str] | Unset):
        registration_endpoint (str | Unset):
        scopes_supported (list[str] | Unset):
        response_types_supported (list[str] | Unset):
        response_modes_supported (list[str] | Unset):
        grant_types_supported (list[str] | Unset):
        subject_types_supported (list[str] | Unset):
        code_challenge_methods_supported (list[str] | Unset):
        token_endpoint_auth_methods_supported (list[str] | Unset):
        service_documentation (str | Unset):
        op_policy_uri (str | Unset):
        op_tos_uri (str | Unset):
    """

    issuer: str | Unset = UNSET
    authorization_endpoint: str | Unset = UNSET
    token_endpoint: str | Unset = UNSET
    device_authorization_endpoint: str | Unset = UNSET
    introspection_endpoint: str | Unset = UNSET
    introspection_endpoint_auth_methods_supported: list[str] | Unset = UNSET
    revocation_endpoint: str | Unset = UNSET
    revocation_endpoint_auth_methods_supported: list[str] | Unset = UNSET
    registration_endpoint: str | Unset = UNSET
    scopes_supported: list[str] | Unset = UNSET
    response_types_supported: list[str] | Unset = UNSET
    response_modes_supported: list[str] | Unset = UNSET
    grant_types_supported: list[str] | Unset = UNSET
    subject_types_supported: list[str] | Unset = UNSET
    code_challenge_methods_supported: list[str] | Unset = UNSET
    token_endpoint_auth_methods_supported: list[str] | Unset = UNSET
    service_documentation: str | Unset = UNSET
    op_policy_uri: str | Unset = UNSET
    op_tos_uri: str | Unset = UNSET
    additional_properties: dict[str, Any] = _attrs_field(init=False, factory=dict)

    def to_dict(self) -> dict[str, Any]:
        issuer = self.issuer

        authorization_endpoint = self.authorization_endpoint

        token_endpoint = self.token_endpoint

        device_authorization_endpoint = self.device_authorization_endpoint

        introspection_endpoint = self.introspection_endpoint

        introspection_endpoint_auth_methods_supported: list[str] | Unset = UNSET
        if not isinstance(self.introspection_endpoint_auth_methods_supported, Unset):
            introspection_endpoint_auth_methods_supported = self.introspection_endpoint_auth_methods_supported

        revocation_endpoint = self.revocation_endpoint

        revocation_endpoint_auth_methods_supported: list[str] | Unset = UNSET
        if not isinstance(self.revocation_endpoint_auth_methods_supported, Unset):
            revocation_endpoint_auth_methods_supported = self.revocation_endpoint_auth_methods_supported

        registration_endpoint = self.registration_endpoint

        scopes_supported: list[str] | Unset = UNSET
        if not isinstance(self.scopes_supported, Unset):
            scopes_supported = self.scopes_supported

        response_types_supported: list[str] | Unset = UNSET
        if not isinstance(self.response_types_supported, Unset):
            response_types_supported = self.response_types_supported

        response_modes_supported: list[str] | Unset = UNSET
        if not isinstance(self.response_modes_supported, Unset):
            response_modes_supported = self.response_modes_supported

        grant_types_supported: list[str] | Unset = UNSET
        if not isinstance(self.grant_types_supported, Unset):
            grant_types_supported = self.grant_types_supported

        subject_types_supported: list[str] | Unset = UNSET
        if not isinstance(self.subject_types_supported, Unset):
            subject_types_supported = self.subject_types_supported

        code_challenge_methods_supported: list[str] | Unset = UNSET
        if not isinstance(self.code_challenge_methods_supported, Unset):
            code_challenge_methods_supported = self.code_challenge_methods_supported

        token_endpoint_auth_methods_supported: list[str] | Unset = UNSET
        if not isinstance(self.token_endpoint_auth_methods_supported, Unset):
            token_endpoint_auth_methods_supported = self.token_endpoint_auth_methods_supported

        service_documentation = self.service_documentation

        op_policy_uri = self.op_policy_uri

        op_tos_uri = self.op_tos_uri

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
        if introspection_endpoint is not UNSET:
            field_dict["introspection_endpoint"] = introspection_endpoint
        if introspection_endpoint_auth_methods_supported is not UNSET:
            field_dict["introspection_endpoint_auth_methods_supported"] = introspection_endpoint_auth_methods_supported
        if revocation_endpoint is not UNSET:
            field_dict["revocation_endpoint"] = revocation_endpoint
        if revocation_endpoint_auth_methods_supported is not UNSET:
            field_dict["revocation_endpoint_auth_methods_supported"] = revocation_endpoint_auth_methods_supported
        if registration_endpoint is not UNSET:
            field_dict["registration_endpoint"] = registration_endpoint
        if scopes_supported is not UNSET:
            field_dict["scopes_supported"] = scopes_supported
        if response_types_supported is not UNSET:
            field_dict["response_types_supported"] = response_types_supported
        if response_modes_supported is not UNSET:
            field_dict["response_modes_supported"] = response_modes_supported
        if grant_types_supported is not UNSET:
            field_dict["grant_types_supported"] = grant_types_supported
        if subject_types_supported is not UNSET:
            field_dict["subject_types_supported"] = subject_types_supported
        if code_challenge_methods_supported is not UNSET:
            field_dict["code_challenge_methods_supported"] = code_challenge_methods_supported
        if token_endpoint_auth_methods_supported is not UNSET:
            field_dict["token_endpoint_auth_methods_supported"] = token_endpoint_auth_methods_supported
        if service_documentation is not UNSET:
            field_dict["service_documentation"] = service_documentation
        if op_policy_uri is not UNSET:
            field_dict["op_policy_uri"] = op_policy_uri
        if op_tos_uri is not UNSET:
            field_dict["op_tos_uri"] = op_tos_uri

        return field_dict

    @classmethod
    def from_dict(cls: type[T], src_dict: Mapping[str, Any]) -> T:
        d = dict(src_dict)
        issuer = d.pop("issuer", UNSET)

        authorization_endpoint = d.pop("authorization_endpoint", UNSET)

        token_endpoint = d.pop("token_endpoint", UNSET)

        device_authorization_endpoint = d.pop("device_authorization_endpoint", UNSET)

        introspection_endpoint = d.pop("introspection_endpoint", UNSET)

        introspection_endpoint_auth_methods_supported = cast(
            list[str], d.pop("introspection_endpoint_auth_methods_supported", UNSET)
        )

        revocation_endpoint = d.pop("revocation_endpoint", UNSET)

        revocation_endpoint_auth_methods_supported = cast(
            list[str], d.pop("revocation_endpoint_auth_methods_supported", UNSET)
        )

        registration_endpoint = d.pop("registration_endpoint", UNSET)

        scopes_supported = cast(list[str], d.pop("scopes_supported", UNSET))

        response_types_supported = cast(list[str], d.pop("response_types_supported", UNSET))

        response_modes_supported = cast(list[str], d.pop("response_modes_supported", UNSET))

        grant_types_supported = cast(list[str], d.pop("grant_types_supported", UNSET))

        subject_types_supported = cast(list[str], d.pop("subject_types_supported", UNSET))

        code_challenge_methods_supported = cast(list[str], d.pop("code_challenge_methods_supported", UNSET))

        token_endpoint_auth_methods_supported = cast(list[str], d.pop("token_endpoint_auth_methods_supported", UNSET))

        service_documentation = d.pop("service_documentation", UNSET)

        op_policy_uri = d.pop("op_policy_uri", UNSET)

        op_tos_uri = d.pop("op_tos_uri", UNSET)

        o_auth_authorization_server_metadata = cls(
            issuer=issuer,
            authorization_endpoint=authorization_endpoint,
            token_endpoint=token_endpoint,
            device_authorization_endpoint=device_authorization_endpoint,
            introspection_endpoint=introspection_endpoint,
            introspection_endpoint_auth_methods_supported=introspection_endpoint_auth_methods_supported,
            revocation_endpoint=revocation_endpoint,
            revocation_endpoint_auth_methods_supported=revocation_endpoint_auth_methods_supported,
            registration_endpoint=registration_endpoint,
            scopes_supported=scopes_supported,
            response_types_supported=response_types_supported,
            response_modes_supported=response_modes_supported,
            grant_types_supported=grant_types_supported,
            subject_types_supported=subject_types_supported,
            code_challenge_methods_supported=code_challenge_methods_supported,
            token_endpoint_auth_methods_supported=token_endpoint_auth_methods_supported,
            service_documentation=service_documentation,
            op_policy_uri=op_policy_uri,
            op_tos_uri=op_tos_uri,
        )

        o_auth_authorization_server_metadata.additional_properties = d
        return o_auth_authorization_server_metadata

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
