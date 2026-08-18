from __future__ import annotations

from collections.abc import Mapping
from typing import Any, TypeVar

from attrs import define as _attrs_define
from attrs import field as _attrs_field

from ..models.health_status_status import HealthStatusStatus
from ..types import UNSET, Unset

T = TypeVar("T", bound="HealthStatus")


@_attrs_define
class HealthStatus:
    """
    Attributes:
        status (HealthStatusStatus):
        service (str | Unset):
        timestamp (int | Unset):
        storage_type (str | Unset):
        database (str | Unset): connected / disconnected / not_configured / unknown
        redis (str | Unset): connected / disconnected / not_configured
    """

    status: HealthStatusStatus
    service: str | Unset = UNSET
    timestamp: int | Unset = UNSET
    storage_type: str | Unset = UNSET
    database: str | Unset = UNSET
    redis: str | Unset = UNSET
    additional_properties: dict[str, Any] = _attrs_field(init=False, factory=dict)

    def to_dict(self) -> dict[str, Any]:
        status = self.status.value

        service = self.service

        timestamp = self.timestamp

        storage_type = self.storage_type

        database = self.database

        redis = self.redis

        field_dict: dict[str, Any] = {}
        field_dict.update(self.additional_properties)
        field_dict.update(
            {
                "status": status,
            }
        )
        if service is not UNSET:
            field_dict["service"] = service
        if timestamp is not UNSET:
            field_dict["timestamp"] = timestamp
        if storage_type is not UNSET:
            field_dict["storage_type"] = storage_type
        if database is not UNSET:
            field_dict["database"] = database
        if redis is not UNSET:
            field_dict["redis"] = redis

        return field_dict

    @classmethod
    def from_dict(cls: type[T], src_dict: Mapping[str, Any]) -> T:
        d = dict(src_dict)
        status = HealthStatusStatus(d.pop("status"))

        service = d.pop("service", UNSET)

        timestamp = d.pop("timestamp", UNSET)

        storage_type = d.pop("storage_type", UNSET)

        database = d.pop("database", UNSET)

        redis = d.pop("redis", UNSET)

        health_status = cls(
            status=status,
            service=service,
            timestamp=timestamp,
            storage_type=storage_type,
            database=database,
            redis=redis,
        )

        health_status.additional_properties = d
        return health_status

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
