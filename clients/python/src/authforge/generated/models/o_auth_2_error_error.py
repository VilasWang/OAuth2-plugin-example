from enum import Enum


class OAuth2ErrorError(str, Enum):
    ACCESS_DENIED = "access_denied"
    AUTHORIZATION_PENDING = "authorization_pending"
    EXPIRED_TOKEN = "expired_token"
    INSUFFICIENT_SCOPE = "insufficient_scope"
    INVALID_CLIENT = "invalid_client"
    INVALID_GRANT = "invalid_grant"
    INVALID_REQUEST = "invalid_request"
    INVALID_SCOPE = "invalid_scope"
    INVALID_TOKEN = "invalid_token"
    SERVER_ERROR = "server_error"
    SLOW_DOWN = "slow_down"
    TEMPORARILY_UNAVAILABLE = "temporarily_unavailable"
    UNAUTHORIZED_CLIENT = "unauthorized_client"
    UNSUPPORTED_GRANT_TYPE = "unsupported_grant_type"
    UNSUPPORTED_TOKEN_TYPE = "unsupported_token_type"

    def __str__(self) -> str:
        return str(self.value)
