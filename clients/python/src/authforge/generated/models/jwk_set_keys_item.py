from __future__ import annotations

from collections.abc import Mapping
from typing import Any, TypeVar, cast

from attrs import define as _attrs_define
from attrs import field as _attrs_field

from ..types import UNSET, Unset

T = TypeVar("T", bound="JWKSetKeysItem")


@_attrs_define
class JWKSetKeysItem:
    """
    Attributes:
        kty (str):
        kid (str | Unset):
        use (str | Unset):
        alg (str | Unset):
        n (str | Unset):
        e (str | Unset):
        x5c (list[str] | Unset):
    """

    kty: str
    kid: str | Unset = UNSET
    use: str | Unset = UNSET
    alg: str | Unset = UNSET
    n: str | Unset = UNSET
    e: str | Unset = UNSET
    x5c: list[str] | Unset = UNSET
    additional_properties: dict[str, Any] = _attrs_field(init=False, factory=dict)

    def to_dict(self) -> dict[str, Any]:
        kty = self.kty

        kid = self.kid

        use = self.use

        alg = self.alg

        n = self.n

        e = self.e

        x5c: list[str] | Unset = UNSET
        if not isinstance(self.x5c, Unset):
            x5c = self.x5c

        field_dict: dict[str, Any] = {}
        field_dict.update(self.additional_properties)
        field_dict.update(
            {
                "kty": kty,
            }
        )
        if kid is not UNSET:
            field_dict["kid"] = kid
        if use is not UNSET:
            field_dict["use"] = use
        if alg is not UNSET:
            field_dict["alg"] = alg
        if n is not UNSET:
            field_dict["n"] = n
        if e is not UNSET:
            field_dict["e"] = e
        if x5c is not UNSET:
            field_dict["x5c"] = x5c

        return field_dict

    @classmethod
    def from_dict(cls: type[T], src_dict: Mapping[str, Any]) -> T:
        d = dict(src_dict)
        kty = d.pop("kty")

        kid = d.pop("kid", UNSET)

        use = d.pop("use", UNSET)

        alg = d.pop("alg", UNSET)

        n = d.pop("n", UNSET)

        e = d.pop("e", UNSET)

        x5c = cast(list[str], d.pop("x5c", UNSET))

        jwk_set_keys_item = cls(
            kty=kty,
            kid=kid,
            use=use,
            alg=alg,
            n=n,
            e=e,
            x5c=x5c,
        )

        jwk_set_keys_item.additional_properties = d
        return jwk_set_keys_item

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
