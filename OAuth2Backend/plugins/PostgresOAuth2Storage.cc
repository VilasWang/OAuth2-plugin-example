#include "PostgresOAuth2Storage.h"
#include <drogon/drogon.h>
#include <drogon/utils/Utilities.h>
#include "OAuth2Metrics.h"

namespace oauth2 {

using namespace drogon::orm;

void PostgresOAuth2Storage::initFromConfig(const Json::Value& config) {
    dbClientName_ = config.get("db_client_name", "default").asString();
    try {
        dbClient_ = drogon::app().getDbClient(dbClientName_);
    } catch (...) {
        LOG_ERROR << "Failed to get DB Client: " << dbClientName_;
    }
}

void PostgresOAuth2Storage::getClient(const std::string& clientId, ClientCallback&& cb) {
    LOG_DEBUG << "Postgres getClient: " << clientId;
    if (!dbClient_) { 
        LOG_ERROR << "Postgres DB Client is null!";
        cb(std::nullopt); return; 
    }
    
    auto sharedCb = std::make_shared<ClientCallback>(std::move(cb));
    auto timer = std::make_shared<OperationTimer>("getClient", "postgres");

    dbClient_->execSqlAsync(
        "SELECT * FROM oauth2_clients WHERE client_id = $1",
        [sharedCb, clientId, timer](const Result& r) {
            if (r.empty()) {
                LOG_DEBUG << "Postgres getClient: Not found -> " << clientId;
                (*sharedCb)(std::nullopt);
                return;
            }
            auto row = r[0];
            OAuth2Client client;
            client.clientId = row["client_id"].as<std::string>();
            LOG_DEBUG << "Postgres getClient: Found -> " << client.clientId;
            client.clientSecretHash = row["client_secret"].as<std::string>();
            client.salt = row["salt"].as<std::string>();
            
            std::string uris = row["redirect_uris"].as<std::string>();
            LOG_DEBUG << "Postgres getClient: Redirect URIs -> " << uris;
            std::stringstream ss(uris);
            std::string uri;
            while (std::getline(ss, uri, ',')) {
                 client.redirectUris.push_back(uri);
            }

            (*sharedCb)(client);
        },
        [sharedCb, clientId](const DrogonDbException& e) {
            LOG_ERROR << "Postgres getClient DB Error for " << clientId << ": " << e.base().what();
            (*sharedCb)(std::nullopt);
        },
        clientId
    );
}

// To fix the "move callback" issue in async calls with multiple branches (success/error):
// We will simply ignore Postgres impl details for now and use a simpler pattern:
// Capture by copy since std::function is copyable. Redefine 'cb' as lvalue in body.

void PostgresOAuth2Storage::validateClient(const std::string& clientId, 
                                           const std::string& clientSecret,
                                           BoolCallback&& cb) {
    LOG_DEBUG << "Postgres validateClient: " << clientId;
    if (!dbClient_) { cb(false); return; }

    // If clientSecret empty, only verify client existence (authorization request)
    if (clientSecret.empty()) {
        auto sharedCb = std::make_shared<BoolCallback>(std::move(cb));
        dbClient_->execSqlAsync(
            "SELECT 1 FROM oauth2_clients WHERE client_id = $1",
            [sharedCb, clientId](const Result& r) {
                bool exists = !r.empty();
                LOG_DEBUG << "Postgres validateClient (no secret): clientId=" << clientId << " exists=" << exists;
                (*sharedCb)(exists);
            },
            [sharedCb, clientId](const DrogonDbException& e) {
                LOG_ERROR << "Postgres validateClient DB Error for " << clientId << ": " << e.base().what();
                (*sharedCb)(false);
            },
            clientId);
        return;
    }

    auto sharedCb = std::make_shared<BoolCallback>(std::move(cb));

    dbClient_->execSqlAsync(
        "SELECT client_secret, salt FROM oauth2_clients WHERE client_id = $1",
        [sharedCb, clientSecret, clientId](const Result& r) {
            if (r.empty()) {
                LOG_DEBUG << "Postgres validateClient: Not found -> " << clientId;
                (*sharedCb)(false);
                return;
            }
            auto row = r[0];
            std::string storedHash = row["client_secret"].as<std::string>();
            std::string salt = row["salt"].as<std::string>();
            
            // Compute hash for validation
            std::string computedHash = drogon::utils::getSha256(clientSecret + salt);
            
            LOG_DEBUG << "Postgres validateClient: storedHash=" << storedHash << ", computedHash=" << computedHash;

            if (computedHash.length() == storedHash.length()) {
                bool match = true;
                for (size_t i = 0; i < computedHash.length(); ++i) {
                    if (std::tolower(computedHash[i]) != std::tolower(storedHash[i])) {
                        match = false;
                        break;
                    }
                }
                LOG_DEBUG << "Postgres validateClient match result: " << match;
                (*sharedCb)(match);
            } else {
                LOG_DEBUG << "Postgres validateClient length mismatch";
                (*sharedCb)(false);
            }
        },
        [sharedCb, clientId](const DrogonDbException& e) {
            LOG_ERROR << "Postgres validateClient DB Error for " << clientId << ": " << e.base().what();
            (*sharedCb)(false);
        },
        clientId
    );
}

// ... Implementing other methods with similar patterns ...
// For brevity in this tool call, I will put placeholders that compilation will accept, 
// as the user is prioritizing Redis.
// I will implement them properly to avoid link errors.

void PostgresOAuth2Storage::saveAuthCode(const OAuth2AuthCode& code, VoidCallback&& cb) {
    if (!dbClient_) { if(cb) cb(); return; }
    auto sharedCb = std::make_shared<VoidCallback>(std::move(cb));
    dbClient_->execSqlAsync(
        "INSERT INTO oauth2_codes (code, client_id, user_id, scope, redirect_uri, expires_at, used) VALUES ($1, $2, $3, $4, $5, $6, $7)",
        [sharedCb](const Result& r) { if(*sharedCb) (*sharedCb)(); },
        [sharedCb](const DrogonDbException& e) {
            LOG_ERROR << "saveAuthCode Error: " << e.base().what();
            if(*sharedCb) (*sharedCb)();
        },
        code.code, code.clientId, code.userId, code.scope, code.redirectUri, code.expiresAt, code.used
    );
}

void PostgresOAuth2Storage::getAuthCode(const std::string& code, AuthCodeCallback&& cb) {
    if (!dbClient_) { cb(std::nullopt); return; }
    auto sharedCb = std::make_shared<AuthCodeCallback>(std::move(cb));
    dbClient_->execSqlAsync(
        "SELECT * FROM oauth2_codes WHERE code = $1",
        [sharedCb](const Result& r) {
            if (r.empty()) { (*sharedCb)(std::nullopt); return; }
            auto row = r[0];
            OAuth2AuthCode c;
            c.code = row["code"].as<std::string>();
            c.clientId = row["client_id"].as<std::string>();
            c.userId = row["user_id"].as<std::string>();
            c.scope = row["scope"].as<std::string>();
            c.redirectUri = row["redirect_uri"].as<std::string>();
            c.expiresAt = row["expires_at"].as<int64_t>();
            c.used = row["used"].as<bool>();
            (*sharedCb)(c);
        },
        [sharedCb](const DrogonDbException& e) {
            LOG_ERROR << "getAuthCode Error: " << e.base().what();
            (*sharedCb)(std::nullopt);
        },
        code
    );
}

void PostgresOAuth2Storage::markAuthCodeUsed(const std::string& code, VoidCallback&& cb) {
    if (!dbClient_) { if(cb) cb(); return; }
    auto sharedCb = std::make_shared<VoidCallback>(std::move(cb));
    dbClient_->execSqlAsync(
        "UPDATE oauth2_codes SET used = true WHERE code = $1",
        [sharedCb](const Result& r) { if(*sharedCb) (*sharedCb)(); },
        [sharedCb](const DrogonDbException& e) {
            LOG_ERROR << "markAuthCodeUsed Error: " << e.base().what();
            if(*sharedCb) (*sharedCb)();
        },
        code
    );
}

void PostgresOAuth2Storage::saveAccessToken(const OAuth2AccessToken& token, VoidCallback&& cb) {
    if (!dbClient_) { if(cb) cb(); return; }
    auto sharedCb = std::make_shared<VoidCallback>(std::move(cb));
    dbClient_->execSqlAsync(
        "INSERT INTO oauth2_access_tokens (token, client_id, user_id, scope, expires_at, revoked) VALUES ($1, $2, $3, $4, $5, $6)",
        [sharedCb](const Result& r) { if(*sharedCb) (*sharedCb)(); },
        [sharedCb](const DrogonDbException& e) {
            LOG_ERROR << "saveAccessToken Error: " << e.base().what();
            if(*sharedCb) (*sharedCb)();
        },
        token.token, token.clientId, token.userId, token.scope, token.expiresAt, token.revoked
    );
}

void PostgresOAuth2Storage::getAccessToken(const std::string& token, AccessTokenCallback&& cb) {
    if (!dbClient_) { cb(std::nullopt); return; }
    auto sharedCb = std::make_shared<AccessTokenCallback>(std::move(cb));
    dbClient_->execSqlAsync(
        "SELECT * FROM oauth2_access_tokens WHERE token = $1",
        [sharedCb](const Result& r) {
            if (r.empty()) { (*sharedCb)(std::nullopt); return; }
            auto row = r[0];
            OAuth2AccessToken t;
            t.token = row["token"].as<std::string>();
            t.clientId = row["client_id"].as<std::string>();
            t.userId = row["user_id"].as<std::string>();
            t.scope = row["scope"].as<std::string>();
            t.expiresAt = row["expires_at"].as<int64_t>();
            t.revoked = row["revoked"].as<bool>();
            (*sharedCb)(t);
        },
        [sharedCb](const DrogonDbException& e) {
            LOG_ERROR << "getAccessToken Error: " << e.base().what();
            (*sharedCb)(std::nullopt);
        },
        token
    );
}

void PostgresOAuth2Storage::saveRefreshToken(const OAuth2RefreshToken& token, VoidCallback&& cb) {
    if (!dbClient_) { if(cb) cb(); return; }
    auto sharedCb = std::make_shared<VoidCallback>(std::move(cb));
    dbClient_->execSqlAsync(
        "INSERT INTO oauth2_refresh_tokens (token, access_token, client_id, user_id, scope, expires_at, revoked) VALUES ($1, $2, $3, $4, $5, $6, $7)",
        [sharedCb](const Result& r) { if(*sharedCb) (*sharedCb)(); },
        [sharedCb](const DrogonDbException& e) {
            LOG_ERROR << "saveRefreshToken Error: " << e.base().what();
            if(*sharedCb) (*sharedCb)();
        },
        token.token, token.accessToken, token.clientId, token.userId, token.scope, token.expiresAt, token.revoked
    );
}

void PostgresOAuth2Storage::getRefreshToken(const std::string& token, RefreshTokenCallback&& cb) {
    if (!dbClient_) { cb(std::nullopt); return; }
    auto sharedCb = std::make_shared<RefreshTokenCallback>(std::move(cb));
    dbClient_->execSqlAsync(
        "SELECT * FROM oauth2_refresh_tokens WHERE token = $1",
        [sharedCb](const Result& r) {
            if (r.empty()) { (*sharedCb)(std::nullopt); return; }
            auto row = r[0];
            OAuth2RefreshToken t;
            t.token = row["token"].as<std::string>();
            t.accessToken = row["access_token"].as<std::string>();
            t.clientId = row["client_id"].as<std::string>();
            t.userId = row["user_id"].as<std::string>();
            t.scope = row["scope"].as<std::string>();
            t.expiresAt = row["expires_at"].as<int64_t>();
            t.revoked = row["revoked"].as<bool>();
            (*sharedCb)(t);
        },
        [sharedCb](const DrogonDbException& e) {
            LOG_ERROR << "getRefreshToken Error: " << e.base().what();
            (*sharedCb)(std::nullopt);
        },
        token
    );
}

} // namespace oauth2
