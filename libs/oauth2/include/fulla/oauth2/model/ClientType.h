#pragma once

// Task 17 slice 2 (fulla-sdk-refactor, design.md §6/§8): libs/oauth2
// Domain model. Ports oauth2::ClientType (currently
// OAuth2Plugin/include/oauth2/types/OAuth2Types.h) verbatim into
// fulla::oauth2::model, since OAuth2Client (this slice's main DTO)
// needs it. GrantType/OAuth2Error (also in that header) are NOT ported
// here: per PROGRESS.md, Task 13 already ported the OAuth2 protocol error
// codes into fulla::common's framework-agnostic ErrorCatalog, so
// OAuth2Error/oauth2ErrorToString/getHttpStatusCode have a superseding
// home already and porting the old enum here would create a second,
// competing error representation.
//
// This header is additive: OAuth2Types.h is untouched, and no production
// call site has been switched to this type yet (that is a later
// TokenService/ClientService/AuthorizationService migration slice).

#include <stdexcept>
#include <string>

namespace fulla::oauth2::model
{

/**
 * @brief OAuth2 Client Types per RFC 6749.
 *
 * CONFIDENTIAL: Clients capable of maintaining the confidentiality of their
 * credentials (e.g. web apps running on a server, backend services).
 * PUBLIC: Clients incapable of maintaining confidentiality (e.g. SPA,
 * mobile, desktop apps).
 */
enum class ClientType
{
    PUBLIC,
    CONFIDENTIAL
};

inline std::string clientTypeToString(ClientType type)
{
    switch (type)
    {
        case ClientType::PUBLIC:
            return "PUBLIC";
        case ClientType::CONFIDENTIAL:
            return "CONFIDENTIAL";
        default:
            return "UNKNOWN";
    }
}

/// @throws std::invalid_argument if `str` is not "PUBLIC" or "CONFIDENTIAL".
inline ClientType stringToClientType(const std::string &str)
{
    if (str == "PUBLIC")
    {
        return ClientType::PUBLIC;
    }
    if (str == "CONFIDENTIAL")
    {
        return ClientType::CONFIDENTIAL;
    }
    throw std::invalid_argument("Invalid ClientType string: " + str);
}

}  // namespace fulla::oauth2::model
