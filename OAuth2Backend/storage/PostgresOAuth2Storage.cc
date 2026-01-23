#include "PostgresOAuth2Storage.h"
#include <drogon/drogon.h>
#include <drogon/utils/Utilities.h>
#include "plugins/OAuth2Metrics.h"

#include "../models/Oauth2Clients.h"
#include "../models/Oauth2Codes.h"
#include "../models/Oauth2AccessTokens.h"
#include "../models/Oauth2RefreshTokens.h"

namespace oauth2
{

using namespace drogon::orm;
using namespace drogon_model::oauth_test;

void PostgresOAuth2Storage::initFromConfig(const Json::Value &config)
{
    dbClientName_ = config.get("db_client_name", "default").asString();
    dbClientReaderName_ =
        config.get("db_client_reader", dbClientName_).asString();

    try
    {
        dbClientMaster_ = drogon::app().getDbClient(dbClientName_);
        dbClientReader_ = drogon::app().getDbClient(dbClientReaderName_);
    }
    catch (...)
    {
        LOG_ERROR << "Failed to get DB Clients: Master=" << dbClientName_
                  << ", Reader=" << dbClientReaderName_;
    }
}

void PostgresOAuth2Storage::getClient(const std::string &clientId,
                                      ClientCallback &&cb)
{
    LOG_DEBUG << "Postgres getClient: " << clientId;
    if (!dbClientReader_)
    {
        LOG_ERROR << "Postgres DB Client Reader is null!";
        cb(std::nullopt);
        return;
    }

    auto sharedCb = std::make_shared<ClientCallback>(std::move(cb));
    try
    {
        Mapper<Oauth2Clients> mapper(dbClientReader_);
        mapper.findOne(
            Criteria(Oauth2Clients::Cols::_client_id,
                     CompareOperator::EQ,
                     clientId),
            [sharedCb, clientId](const Oauth2Clients &row) {
                OAuth2Client client;
                client.clientId = row.getValueOfClientId();
                LOG_DEBUG << "Postgres getClient: Found -> " << client.clientId;
                client.clientSecretHash = row.getValueOfClientSecret();
                client.salt = row.getValueOfSalt();

                std::string uris = row.getValueOfRedirectUris();
                LOG_DEBUG << "Postgres getClient: Redirect URIs -> " << uris;
                std::stringstream ss(uris);
                std::string uri;
                while (std::getline(ss, uri, ','))
                {
                    client.redirectUris.push_back(uri);
                }
                (*sharedCb)(client);
            },
            [sharedCb, clientId](const DrogonDbException &e) {
                LOG_DEBUG << "Postgres getClient: Not found or Error -> "
                          << clientId << " (" << e.base().what() << ")";
                // FindOne throws or calls unexpected error callback if not
                // found? Actually generated findOne typically throws if 0 rows
                // in sync. Async: Exception callback is called for DB errors.
                // If not found, does it call exception or success?
                // Mapper::findOne async usually expects exactly one. If not
                // found, it often calls exception callback with specific
                // UnexpectedRows or similar. Wait, standard Mapper findOne
                // calls exception callback if row count != 1.
                (*sharedCb)(std::nullopt);
            });
    }
    catch (...)
    {
        LOG_ERROR << "Postgres getClient Exception";
        (*sharedCb)(std::nullopt);
    }
}

// To fix the "move callback" issue in async calls with multiple branches
// (success/error): We will simply ignore Postgres impl details for now and use
// a simpler pattern: Capture by copy since std::function is copyable. Redefine
// 'cb' as lvalue in body.

void PostgresOAuth2Storage::validateClient(const std::string &clientId,
                                           const std::string &clientSecret,
                                           IOAuth2Storage::BoolCallback &&cb)
{
    LOG_DEBUG << "Postgres validateClient: " << clientId;
    if (!dbClientReader_)
    {
        cb(false);
        return;
    }

    auto sharedCb = std::make_shared<BoolCallback>(std::move(cb));
    try
    {
        Mapper<Oauth2Clients> mapper(dbClientReader_);

        // Case 1: No Client Secret (Check Existence Only via PK)
        // If clientSecret is empty, we just check if client exists.
        if (clientSecret.empty())
        {
            mapper.findOne(
                Criteria(Oauth2Clients::Cols::_client_id,
                         CompareOperator::EQ,
                         clientId),
                [sharedCb, clientId](const Oauth2Clients &) {
                    LOG_DEBUG
                        << "Postgres validateClient (no secret): Found -> "
                        << clientId;
                    (*sharedCb)(true);
                },
                [sharedCb, clientId](const DrogonDbException &e) {
                    LOG_DEBUG << "Postgres validateClient (no secret): Not "
                                 "found/Error -> "
                              << clientId << " " << e.base().what();
                    (*sharedCb)(false);
                });
            return;
        }

        // Case 2: Validate Secret
        mapper.findOne(
            Criteria(Oauth2Clients::Cols::_client_id,
                     CompareOperator::EQ,
                     clientId),
            [sharedCb, clientId, clientSecret](const Oauth2Clients &row) {
                std::string storedHash = row.getValueOfClientSecret();
                std::string salt = row.getValueOfSalt();

                // Compute hash for validation
                std::string computedHash =
                    drogon::utils::getSha256(clientSecret + salt);

                LOG_DEBUG << "Postgres validateClient: storedHash="
                          << storedHash << ", computedHash=" << computedHash;

                if (computedHash.length() == storedHash.length())
                {
                    bool match = true;
                    for (size_t i = 0; i < computedHash.length(); ++i)
                    {
                        if (std::tolower(computedHash[i]) !=
                            std::tolower(storedHash[i]))
                        {
                            match = false;
                            break;
                        }
                    }
                    LOG_DEBUG << "Postgres validateClient match result: "
                              << match;
                    (*sharedCb)(match);
                }
                else
                {
                    LOG_DEBUG << "Postgres validateClient length mismatch";
                    (*sharedCb)(false);
                }
            },
            [sharedCb, clientId](const DrogonDbException &e) {
                LOG_ERROR << "Postgres validateClient Error for " << clientId
                          << ": " << e.base().what();
                (*sharedCb)(false);
            });
    }
    catch (...)
    {
        LOG_ERROR << "Postgres validateClient Exception";
        (*sharedCb)(false);
    }
}

// ... Implementing other methods with similar patterns ...
// For brevity in this tool call, I will put placeholders that compilation will
// accept, as the user is prioritizing Redis. I will implement them properly to
// avoid link errors.

void PostgresOAuth2Storage::saveAuthCode(const oauth2::OAuth2AuthCode &code,
                                         IOAuth2Storage::VoidCallback &&cb)
{
    if (!dbClientMaster_)
    {
        if (cb)
            cb();
        return;
    }
    auto sharedCb = std::make_shared<VoidCallback>(std::move(cb));
    try
    {
        Mapper<Oauth2Codes> mapper(dbClientMaster_);
        Oauth2Codes newCode;
        newCode.setCode(code.code);
        newCode.setClientId(code.clientId);
        newCode.setUserId(code.userId);
        newCode.setScope(code.scope);
        newCode.setRedirectUri(code.redirectUri);
        newCode.setExpiresAt(code.expiresAt);
        newCode.setUsed(code.used);

        mapper.insert(
            newCode,
            [sharedCb](const Oauth2Codes &) {
                if (*sharedCb)
                    (*sharedCb)();
            },
            [sharedCb](const DrogonDbException &e) {
                LOG_ERROR << "saveAuthCode Error: " << e.base().what();
                if (*sharedCb)
                    (*sharedCb)();
            });
    }
    catch (...)
    {
        LOG_ERROR << "saveAuthCode Exception";
        if (*sharedCb)
            (*sharedCb)();
    }
}

void PostgresOAuth2Storage::getAuthCode(const std::string &code,
                                        IOAuth2Storage::AuthCodeCallback &&cb)
{
    if (!dbClientReader_)
    {
        cb(std::nullopt);
        return;
    }
    auto sharedCb = std::make_shared<AuthCodeCallback>(std::move(cb));
    try
    {
        Mapper<Oauth2Codes> mapper(dbClientReader_);
        mapper.findOne(
            Criteria(Oauth2Codes::Cols::_code, CompareOperator::EQ, code),
            [sharedCb](const Oauth2Codes &row) {
                OAuth2AuthCode c;
                c.code = row.getValueOfCode();
                c.clientId = row.getValueOfClientId();
                c.userId = row.getValueOfUserId();
                c.scope = row.getValueOfScope();
                c.redirectUri = row.getValueOfRedirectUri();
                c.expiresAt = row.getValueOfExpiresAt();  // int64_t
                c.used = row.getValueOfUsed();
                (*sharedCb)(c);
            },
            [sharedCb](const DrogonDbException &e) {
                // Not found or error
                LOG_DEBUG << "getAuthCode not found or error: "
                          << e.base().what();
                (*sharedCb)(std::nullopt);
            });
    }
    catch (...)
    {
        LOG_ERROR << "getAuthCode Exception";
        (*sharedCb)(std::nullopt);
    }
}

void PostgresOAuth2Storage::markAuthCodeUsed(const std::string &code,
                                             IOAuth2Storage::VoidCallback &&cb)
{
    if (!dbClientMaster_)
    {
        if (cb)
            cb();
        return;
    }
    auto sharedCb = std::make_shared<VoidCallback>(std::move(cb));
    try
    {
        Mapper<Oauth2Codes> mapper(dbClientMaster_);
        Oauth2Codes updateObj;
        updateObj.setCode(code);
        updateObj.setUsed(true);

        mapper.update(
            updateObj,
            [sharedCb](const size_t count) {
                if (*sharedCb)
                    (*sharedCb)();
            },
            [sharedCb](const DrogonDbException &e) {
                LOG_ERROR << "markAuthCodeUsed Error: " << e.base().what();
                if (*sharedCb)
                    (*sharedCb)();
            });
    }
    catch (...)
    {
        LOG_ERROR << "markAuthCodeUsed Exception";
        if (*sharedCb)
            (*sharedCb)();
    }
}

void PostgresOAuth2Storage::consumeAuthCode(
    const std::string &code,
    IOAuth2Storage::AuthCodeCallback &&cb)
{
    if (!dbClientMaster_)
    {
        cb(std::nullopt);
        return;
    }
    auto sharedCb = std::make_shared<AuthCodeCallback>(std::move(cb));

    // Atomic Check-and-Set via UPDATE RETURNING
    // We only update if used=false.
    // If used=true already, WHERE clause fails, returns 0 rows -> cb(nullopt).
    dbClientMaster_->execSqlAsync(
        "UPDATE oauth2_codes SET used = true WHERE code = $1 AND used = false "
        "RETURNING client_id, user_id, scope, redirect_uri, expires_at",
        [sharedCb, code](const Result &r) {
            if (r.empty())
            {
                // Either didn't exist OR was already used.
                // We treat both as failure to consume.
                (*sharedCb)(std::nullopt);
                return;
            }
            auto row = r[0];
            OAuth2AuthCode c;
            c.code = code;
            c.clientId = row["client_id"].as<std::string>();
            c.userId = row["user_id"].as<std::string>();
            c.scope = row["scope"].as<std::string>();
            c.redirectUri = row["redirect_uri"].as<std::string>();
            c.expiresAt = row["expires_at"].as<int64_t>();
            c.used = true;
            (*sharedCb)(c);
        },
        [sharedCb](const DrogonDbException &e) {
            LOG_ERROR << "consumeAuthCode Postgres Error: " << e.base().what();
            (*sharedCb)(std::nullopt);
        },
        code);
}

void PostgresOAuth2Storage::saveAccessToken(
    const oauth2::OAuth2AccessToken &token,
    IOAuth2Storage::VoidCallback &&cb)
{
    if (!dbClientMaster_)
    {
        if (cb)
            cb();
        return;
    }
    auto sharedCb = std::make_shared<VoidCallback>(std::move(cb));
    try
    {
        Mapper<Oauth2AccessTokens> mapper(dbClientMaster_);
        Oauth2AccessTokens newToken;
        newToken.setToken(token.token);
        newToken.setClientId(token.clientId);
        newToken.setUserId(token.userId);
        newToken.setScope(token.scope);
        newToken.setExpiresAt(token.expiresAt);
        newToken.setRevoked(token.revoked);

        mapper.insert(
            newToken,
            [sharedCb](const Oauth2AccessTokens &) {
                if (*sharedCb)
                    (*sharedCb)();
            },
            [sharedCb](const DrogonDbException &e) {
                LOG_ERROR << "saveAccessToken Error: " << e.base().what();
                if (*sharedCb)
                    (*sharedCb)();
            });
    }
    catch (...)
    {
        LOG_ERROR << "saveAccessToken Exception";
        if (*sharedCb)
            (*sharedCb)();
    }
}

void PostgresOAuth2Storage::getAccessToken(
    const std::string &token,
    IOAuth2Storage::AccessTokenCallback &&cb)
{
    if (!dbClientReader_)
    {
        cb(std::nullopt);
        return;
    }
    auto sharedCb = std::make_shared<AccessTokenCallback>(std::move(cb));
    try
    {
        Mapper<Oauth2AccessTokens> mapper(dbClientReader_);
        mapper.findOne(
            Criteria(Oauth2AccessTokens::Cols::_token,
                     CompareOperator::EQ,
                     token),
            [sharedCb](const Oauth2AccessTokens &row) {
                OAuth2AccessToken t;
                t.token = row.getValueOfToken();
                t.clientId = row.getValueOfClientId();
                t.userId = row.getValueOfUserId();
                t.scope = row.getValueOfScope();
                t.expiresAt = row.getValueOfExpiresAt();
                t.revoked = row.getValueOfRevoked();
                (*sharedCb)(t);
            },
            [sharedCb](const DrogonDbException &e) {
                LOG_DEBUG << "getAccessToken not found/error: "
                          << e.base().what();
                (*sharedCb)(std::nullopt);
            });
    }
    catch (...)
    {
        LOG_ERROR << "getAccessToken Exception";
        (*sharedCb)(std::nullopt);
    }
}

void PostgresOAuth2Storage::saveRefreshToken(
    const oauth2::OAuth2RefreshToken &token,
    IOAuth2Storage::VoidCallback &&cb)
{
    if (!dbClientMaster_)
    {
        if (cb)
            cb();
        return;
    }
    auto sharedCb = std::make_shared<VoidCallback>(std::move(cb));
    try
    {
        Mapper<Oauth2RefreshTokens> mapper(dbClientMaster_);
        Oauth2RefreshTokens newToken;
        newToken.setToken(token.token);
        newToken.setAccessToken(token.accessToken);
        newToken.setClientId(token.clientId);
        newToken.setUserId(token.userId);
        newToken.setScope(token.scope);
        newToken.setExpiresAt(token.expiresAt);
        newToken.setRevoked(token.revoked);

        mapper.insert(
            newToken,
            [sharedCb](const Oauth2RefreshTokens &) {
                if (*sharedCb)
                    (*sharedCb)();
            },
            [sharedCb](const DrogonDbException &e) {
                LOG_ERROR << "saveRefreshToken Error: " << e.base().what();
                if (*sharedCb)
                    (*sharedCb)();
            });
    }
    catch (...)
    {
        LOG_ERROR << "saveRefreshToken Exception";
        if (*sharedCb)
            (*sharedCb)();
    }
}

void PostgresOAuth2Storage::getRefreshToken(
    const std::string &token,
    IOAuth2Storage::RefreshTokenCallback &&cb)
{
    if (!dbClientReader_)
    {
        cb(std::nullopt);
        return;
    }
    auto sharedCb = std::make_shared<RefreshTokenCallback>(std::move(cb));
    try
    {
        Mapper<Oauth2RefreshTokens> mapper(dbClientReader_);
        mapper.findOne(
            Criteria(Oauth2RefreshTokens::Cols::_token,
                     CompareOperator::EQ,
                     token),
            [sharedCb](const Oauth2RefreshTokens &row) {
                OAuth2RefreshToken t;
                t.token = row.getValueOfToken();
                t.accessToken = row.getValueOfAccessToken();
                t.clientId = row.getValueOfClientId();
                t.userId = row.getValueOfUserId();
                t.scope = row.getValueOfScope();
                t.expiresAt = row.getValueOfExpiresAt();
                t.revoked = row.getValueOfRevoked();
                (*sharedCb)(t);
            },
            [sharedCb](const DrogonDbException &e) {
                LOG_DEBUG << "getRefreshToken not found/error: "
                          << e.base().what();
                (*sharedCb)(std::nullopt);
            });
    }
    catch (...)
    {
        LOG_ERROR << "getRefreshToken Exception";
        (*sharedCb)(std::nullopt);
    }
}

void PostgresOAuth2Storage::deleteExpiredData()
{
    if (!dbClientMaster_)
        return;

    auto now = std::chrono::duration_cast<std::chrono::seconds>(
                   std::chrono::system_clock::now().time_since_epoch())
                   .count();

    try
    {
        // 1. Codes
        Mapper<Oauth2Codes> codeMapper(dbClientMaster_);
        codeMapper.deleteBy(
            Criteria(Oauth2Codes::Cols::_expires_at, CompareOperator::LT, now),
            [](const size_t count) {
                if (count > 0)
                    LOG_INFO << "Cleaned " << count << " expired auth codes";
            },
            [](const DrogonDbException &e) {
                LOG_ERROR << "Cleanup Codes Error: " << e.base().what();
            });

        // 2. Access Tokens
        Mapper<Oauth2AccessTokens> atMapper(dbClientMaster_);
        atMapper.deleteBy(
            Criteria(Oauth2AccessTokens::Cols::_expires_at,
                     CompareOperator::LT,
                     now),
            [](const size_t count) {
                if (count > 0)
                    LOG_INFO << "Cleaned " << count << " expired access tokens";
            },
            [](const DrogonDbException &e) {
                LOG_ERROR << "Cleanup AccessTokens Error: " << e.base().what();
            });

        // 3. Refresh Tokens
        Mapper<Oauth2RefreshTokens> rtMapper(dbClientMaster_);
        rtMapper.deleteBy(
            Criteria(Oauth2RefreshTokens::Cols::_expires_at,
                     CompareOperator::LT,
                     now),
            [](const size_t count) {
                if (count > 0)
                    LOG_INFO << "Cleaned " << count
                             << " expired refresh tokens";
            },
            [](const DrogonDbException &e) {
                LOG_ERROR << "Cleanup RefreshTokens Error: " << e.base().what();
            });
    }
    catch (...)
    {
        LOG_ERROR << "Cleanup Exception";
    }
}

// RBAC Implementation
void PostgresOAuth2Storage::getUserRoles(const std::string &userId,
                                         StringListCallback &&cb)
{
    if (!dbClientReader_)
    {
        cb({});
        return;
    }

    int uid = 0;
    try
    {
        uid = std::stoi(userId);
    }
    catch (...)
    {
        LOG_WARN << "getUserRoles: Invalid userId (not int): " << userId;
        cb({});
        return;
    }

    // Query: SELECT r.name FROM roles r JOIN user_roles ur ON r.id = ur.role_id
    // WHERE ur.user_id = $1
    std::string sql =
        "SELECT r.name FROM roles r "
        "JOIN user_roles ur ON r.id = ur.role_id "
        "WHERE ur.user_id = $1";

    dbClientReader_->execSqlAsync(
        sql,
        [cb](const Result &r) {
            std::vector<std::string> roles;
            for (const auto &row : r)
            {
                roles.push_back(row["name"].as<std::string>());
            }
            cb(roles);
        },
        [cb](const DrogonDbException &e) {
            LOG_ERROR << "getUserRoles failed: " << e.base().what();
            cb({});
        },
        uid);
}

}  // namespace oauth2
