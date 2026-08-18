from enum import Enum


class PostOauth2ConsentAction(str, Enum):
    APPROVE = "approve"
    DENY = "deny"

    def __str__(self) -> str:
        return str(self.value)
