from enum import Enum


class TokenRequestGrantType(str, Enum):
    AUTHORIZATION_CODE = "authorization_code"
    CLIENT_CREDENTIALS = "client_credentials"
    REFRESH_TOKEN = "refresh_token"
    URNIETFPARAMSOAUTHGRANT_TYPEDEVICE_CODE = "urn:ietf:params:oauth:grant-type:device_code"

    def __str__(self) -> str:
        return str(self.value)
