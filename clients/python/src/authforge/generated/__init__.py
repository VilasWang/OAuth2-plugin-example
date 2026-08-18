"""A client library for accessing OAuth2 Authorization Server API"""

from .client import AuthenticatedClient, Client

__all__ = (
    "AuthenticatedClient",
    "Client",
)
