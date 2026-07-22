#pragma once

// A3 (authforge-sdk-refactor): standalone callback type aliases + the
// AuthorizationTransaction aggregate that the legacy identity-side repository
// interfaces (oauth2::IRoleRepository / IUserRepository /
// ISubjectMappingRepository / IGrantRepository) and their Memory/Postgres/Redis
// implementations borrow. These used to be nested in the god IOAuth2Storage.h
// (now deleted); they live here so the identity interfaces compile without the
// god facade. The identity-side migration to authforge::identity::* (which
// removes these legacy interfaces entirely) is a separate follow-up.
//
// AuthorizationTransaction is field-identical to
// authforge::oauth2::model::AuthorizationTransaction (libs/oauth2 Dto.h); the
// legacy oauth2:: copy is kept only because the legacy identity interfaces
// reference it by the old (nested) name.

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include <json/json.h>

namespace oauth2
{

using StringListCallback = std::function<void(std::vector<std::string>)>;
using OptionalJsonCallback = std::function<void(std::optional<Json::Value>)>;
using OptionalIntCallback = std::function<void(std::optional<int32_t>)>;
using BoolCallback = std::function<void(bool)>;

struct AuthorizationTransaction
{
    std::string transactionId;
    std::string clientId;
    std::string subject;
    std::string redirectUri;
    std::string state;
    std::string codeChallenge;
    std::string codeChallengeMethod;
    std::vector<std::string> requestedScopes;
    std::vector<std::string> validScopes;
    std::vector<std::string> consentRequiredScopes;
    bool consumed = false;
    int64_t expiresAt;
};

}  // namespace oauth2
