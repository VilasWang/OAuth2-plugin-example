#include "PostgresOAuth2Storage.h"
#include <drogon/drogon.h>
#include <drogon/utils/Utilities.h>

namespace oauth2 {

PostgresOAuth2Storage::PostgresOAuth2Storage(const std::string& dbClientName)
    : dbClientName_(dbClientName)
{
}

void PostgresOAuth2Storage::initFromConfig(const Json::Value& config)
{
    if (config.isMember("db_client_name")) {
        dbClientName_ = config["db_client_name"].asString();
    }
    LOG_INFO << "PostgresOAuth2Storage initialized with DbClient: " << dbClientName_;
}

drogon::orm::DbClientPtr PostgresOAuth2Storage::getDbClient() const
{
    return drogon::app().getDbClient(dbClientName_);
}

std::optional<OAuth2Client> PostgresOAuth2Storage::getClient(const std::string& clientId)
{
    auto dbClient = getDbClient();
    if (!dbClient) {
        LOG_ERROR << "Database client not available";
        return std::nullopt;
    }

    try {
        auto result = dbClient->execSqlSync(
            "SELECT client_id, client_secret, redirect_uris, allowed_scopes "
            "FROM oauth2_clients WHERE client_id = $1",
            clientId
        );

        if (result.empty()) {
            return std::nullopt;
        }

        OAuth2Client client;
        client.clientId = result[0]["client_id"].as<std::string>();
        client.clientSecretHash = result[0]["client_secret"].as<std::string>();
        
        // Parse JSON arrays
        Json::Value uris;
        Json::Reader reader;
        if (reader.parse(result[0]["redirect_uris"].as<std::string>(), uris)) {
            for (const auto& uri : uris) {
                client.redirectUris.push_back(uri.asString());
            }
        }
        
        Json::Value scopes;
        if (reader.parse(result[0]["allowed_scopes"].as<std::string>(), scopes)) {
            for (const auto& scope : scopes) {
                client.allowedScopes.push_back(scope.asString());
            }
        }

        return client;
    } catch (const drogon::orm::DrogonDbException& e) {
        LOG_ERROR << "Database error in getClient: " << e.base().what();
        return std::nullopt;
    }
}

bool PostgresOAuth2Storage::validateClient(const std::string& clientId, 
                                           const std::string& clientSecret)
{
    auto client = getClient(clientId);
    if (!client) {
        return false;
    }
    
    if (clientSecret.empty()) {
        return true;
    }
    
    return verifyPassword(clientSecret, client->clientSecretHash);
}

bool PostgresOAuth2Storage::verifyPassword(const std::string& password, 
                                           const std::string& hash) const
{
    // For production, use bcrypt verification
    // This is a placeholder - integrate with a bcrypt library
    // Example: return bcrypt_checkpw(password.c_str(), hash.c_str()) == 0;
    
    // Temporary: plain comparison (NOT FOR PRODUCTION)
    LOG_WARN << "Using plain password comparison - integrate bcrypt for production!";
    return password == hash;
}

void PostgresOAuth2Storage::saveAuthCode(const OAuth2AuthCode& code)
{
    auto dbClient = getDbClient();
    if (!dbClient) {
        LOG_ERROR << "Database client not available";
        return;
    }

    try {
        dbClient->execSqlSync(
            "INSERT INTO oauth2_authorization_codes "
            "(code, client_id, user_id, scope, redirect_uri, code_challenge, "
            "code_challenge_method, expires_at, used) "
            "VALUES ($1, $2, $3, $4, $5, $6, $7, NOW() + interval '10 minutes', $8)",
            code.code, code.clientId, code.userId, code.scope, 
            code.redirectUri, code.codeChallenge, code.codeChallengeMethod,
            code.used
        );
    } catch (const drogon::orm::DrogonDbException& e) {
        LOG_ERROR << "Database error in saveAuthCode: " << e.base().what();
    }
}

std::optional<OAuth2AuthCode> PostgresOAuth2Storage::getAuthCode(const std::string& code)
{
    auto dbClient = getDbClient();
    if (!dbClient) {
        return std::nullopt;
    }

    try {
        auto result = dbClient->execSqlSync(
            "SELECT code, client_id, user_id, scope, redirect_uri, "
            "code_challenge, code_challenge_method, "
            "EXTRACT(EPOCH FROM expires_at)::bigint as expires_at, used "
            "FROM oauth2_authorization_codes "
            "WHERE code = $1 AND used = FALSE AND expires_at > NOW()",
            code
        );

        if (result.empty()) {
            return std::nullopt;
        }

        OAuth2AuthCode authCode;
        authCode.code = result[0]["code"].as<std::string>();
        authCode.clientId = result[0]["client_id"].as<std::string>();
        authCode.userId = result[0]["user_id"].as<std::string>();
        authCode.scope = result[0]["scope"].as<std::string>();
        authCode.redirectUri = result[0]["redirect_uri"].as<std::string>();
        authCode.codeChallenge = result[0]["code_challenge"].as<std::string>();
        authCode.codeChallengeMethod = result[0]["code_challenge_method"].as<std::string>();
        authCode.expiresAt = result[0]["expires_at"].as<int64_t>();
        authCode.used = result[0]["used"].as<bool>();

        return authCode;
    } catch (const drogon::orm::DrogonDbException& e) {
        LOG_ERROR << "Database error in getAuthCode: " << e.base().what();
        return std::nullopt;
    }
}

void PostgresOAuth2Storage::markAuthCodeUsed(const std::string& code)
{
    auto dbClient = getDbClient();
    if (!dbClient) return;

    try {
        dbClient->execSqlSync(
            "UPDATE oauth2_authorization_codes SET used = TRUE WHERE code = $1",
            code
        );
    } catch (const drogon::orm::DrogonDbException& e) {
        LOG_ERROR << "Database error in markAuthCodeUsed: " << e.base().what();
    }
}

void PostgresOAuth2Storage::cleanupExpiredAuthCodes()
{
    auto dbClient = getDbClient();
    if (!dbClient) return;

    try {
        dbClient->execSqlSync(
            "DELETE FROM oauth2_authorization_codes WHERE expires_at < NOW() OR used = TRUE"
        );
    } catch (const drogon::orm::DrogonDbException& e) {
        LOG_ERROR << "Database error in cleanupExpiredAuthCodes: " << e.base().what();
    }
}

void PostgresOAuth2Storage::saveAccessToken(const OAuth2AccessToken& token)
{
    auto dbClient = getDbClient();
    if (!dbClient) return;

    try {
        dbClient->execSqlSync(
            "INSERT INTO oauth2_access_tokens "
            "(token, client_id, user_id, scope, expires_at, revoked) "
            "VALUES ($1, $2, $3, $4, NOW() + interval '1 hour', $5)",
            token.token, token.clientId, token.userId, 
            token.scope, token.revoked
        );
    } catch (const drogon::orm::DrogonDbException& e) {
        LOG_ERROR << "Database error in saveAccessToken: " << e.base().what();
    }
}

std::optional<OAuth2AccessToken> PostgresOAuth2Storage::getAccessToken(const std::string& token)
{
    auto dbClient = getDbClient();
    if (!dbClient) return std::nullopt;

    try {
        auto result = dbClient->execSqlSync(
            "SELECT token, client_id, user_id, scope, "
            "EXTRACT(EPOCH FROM expires_at)::bigint as expires_at, revoked "
            "FROM oauth2_access_tokens "
            "WHERE token = $1 AND revoked = FALSE AND expires_at > NOW()",
            token
        );

        if (result.empty()) {
            return std::nullopt;
        }

        OAuth2AccessToken accessToken;
        accessToken.token = result[0]["token"].as<std::string>();
        accessToken.clientId = result[0]["client_id"].as<std::string>();
        accessToken.userId = result[0]["user_id"].as<std::string>();
        accessToken.scope = result[0]["scope"].as<std::string>();
        accessToken.expiresAt = result[0]["expires_at"].as<int64_t>();
        accessToken.revoked = result[0]["revoked"].as<bool>();

        return accessToken;
    } catch (const drogon::orm::DrogonDbException& e) {
        LOG_ERROR << "Database error in getAccessToken: " << e.base().what();
        return std::nullopt;
    }
}

void PostgresOAuth2Storage::revokeAccessToken(const std::string& token)
{
    auto dbClient = getDbClient();
    if (!dbClient) return;

    try {
        dbClient->execSqlSync(
            "UPDATE oauth2_access_tokens SET revoked = TRUE WHERE token = $1",
            token
        );
    } catch (const drogon::orm::DrogonDbException& e) {
        LOG_ERROR << "Database error in revokeAccessToken: " << e.base().what();
    }
}

void PostgresOAuth2Storage::revokeAllUserTokens(const std::string& userId)
{
    auto dbClient = getDbClient();
    if (!dbClient) return;

    try {
        dbClient->execSqlSync(
            "UPDATE oauth2_access_tokens SET revoked = TRUE WHERE user_id = $1",
            userId
        );
        dbClient->execSqlSync(
            "UPDATE oauth2_refresh_tokens SET revoked = TRUE WHERE user_id = $1",
            userId
        );
    } catch (const drogon::orm::DrogonDbException& e) {
        LOG_ERROR << "Database error in revokeAllUserTokens: " << e.base().what();
    }
}

void PostgresOAuth2Storage::saveRefreshToken(const OAuth2RefreshToken& token)
{
    auto dbClient = getDbClient();
    if (!dbClient) return;

    try {
        dbClient->execSqlSync(
            "INSERT INTO oauth2_refresh_tokens "
            "(token, access_token, client_id, user_id, scope, expires_at, revoked) "
            "VALUES ($1, $2, $3, $4, $5, to_timestamp($6), $7)",
            token.token, token.accessToken, token.clientId, 
            token.userId, token.scope, token.expiresAt, token.revoked
        );
    } catch (const drogon::orm::DrogonDbException& e) {
        LOG_ERROR << "Database error in saveRefreshToken: " << e.base().what();
    }
}

std::optional<OAuth2RefreshToken> PostgresOAuth2Storage::getRefreshToken(const std::string& token)
{
    auto dbClient = getDbClient();
    if (!dbClient) return std::nullopt;

    try {
        auto result = dbClient->execSqlSync(
            "SELECT token, access_token, client_id, user_id, scope, "
            "EXTRACT(EPOCH FROM expires_at)::bigint as expires_at, revoked "
            "FROM oauth2_refresh_tokens "
            "WHERE token = $1 AND revoked = FALSE AND expires_at > NOW()",
            token
        );

        if (result.empty()) {
            return std::nullopt;
        }

        OAuth2RefreshToken refreshToken;
        refreshToken.token = result[0]["token"].as<std::string>();
        refreshToken.accessToken = result[0]["access_token"].as<std::string>();
        refreshToken.clientId = result[0]["client_id"].as<std::string>();
        refreshToken.userId = result[0]["user_id"].as<std::string>();
        refreshToken.scope = result[0]["scope"].as<std::string>();
        refreshToken.expiresAt = result[0]["expires_at"].as<int64_t>();
        refreshToken.revoked = result[0]["revoked"].as<bool>();

        return refreshToken;
    } catch (const drogon::orm::DrogonDbException& e) {
        LOG_ERROR << "Database error in getRefreshToken: " << e.base().what();
        return std::nullopt;
    }
}

void PostgresOAuth2Storage::revokeRefreshToken(const std::string& token)
{
    auto dbClient = getDbClient();
    if (!dbClient) return;

    try {
        dbClient->execSqlSync(
            "UPDATE oauth2_refresh_tokens SET revoked = TRUE WHERE token = $1",
            token
        );
    } catch (const drogon::orm::DrogonDbException& e) {
        LOG_ERROR << "Database error in revokeRefreshToken: " << e.base().what();
    }
}

} // namespace oauth2
