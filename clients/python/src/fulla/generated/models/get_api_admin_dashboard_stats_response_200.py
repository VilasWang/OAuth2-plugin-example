from __future__ import annotations

from collections.abc import Mapping
from typing import Any, TypeVar

from attrs import define as _attrs_define
from attrs import field as _attrs_field

from ..types import UNSET, Unset

T = TypeVar("T", bound="GetApiAdminDashboardStatsResponse200")


@_attrs_define
class GetApiAdminDashboardStatsResponse200:
    """
    Attributes:
        active_tokens (int | Unset):
        failures_today (int | Unset):
        logs_today (int | Unset):
        status (str | Unset):
        total_clients (int | Unset):
        total_users (int | Unset):
    """

    active_tokens: int | Unset = UNSET
    failures_today: int | Unset = UNSET
    logs_today: int | Unset = UNSET
    status: str | Unset = UNSET
    total_clients: int | Unset = UNSET
    total_users: int | Unset = UNSET
    additional_properties: dict[str, Any] = _attrs_field(init=False, factory=dict)

    def to_dict(self) -> dict[str, Any]:
        active_tokens = self.active_tokens

        failures_today = self.failures_today

        logs_today = self.logs_today

        status = self.status

        total_clients = self.total_clients

        total_users = self.total_users

        field_dict: dict[str, Any] = {}
        field_dict.update(self.additional_properties)
        field_dict.update({})
        if active_tokens is not UNSET:
            field_dict["active_tokens"] = active_tokens
        if failures_today is not UNSET:
            field_dict["failures_today"] = failures_today
        if logs_today is not UNSET:
            field_dict["logs_today"] = logs_today
        if status is not UNSET:
            field_dict["status"] = status
        if total_clients is not UNSET:
            field_dict["total_clients"] = total_clients
        if total_users is not UNSET:
            field_dict["total_users"] = total_users

        return field_dict

    @classmethod
    def from_dict(cls: type[T], src_dict: Mapping[str, Any]) -> T:
        d = dict(src_dict)
        active_tokens = d.pop("active_tokens", UNSET)

        failures_today = d.pop("failures_today", UNSET)

        logs_today = d.pop("logs_today", UNSET)

        status = d.pop("status", UNSET)

        total_clients = d.pop("total_clients", UNSET)

        total_users = d.pop("total_users", UNSET)

        get_api_admin_dashboard_stats_response_200 = cls(
            active_tokens=active_tokens,
            failures_today=failures_today,
            logs_today=logs_today,
            status=status,
            total_clients=total_clients,
            total_users=total_users,
        )

        get_api_admin_dashboard_stats_response_200.additional_properties = d
        return get_api_admin_dashboard_stats_response_200

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
