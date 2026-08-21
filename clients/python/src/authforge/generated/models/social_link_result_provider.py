from enum import Enum


class SocialLinkResultProvider(str, Enum):
    GITHUB = "github"
    GOOGLE = "google"
    WECHAT = "wechat"

    def __str__(self) -> str:
        return str(self.value)
