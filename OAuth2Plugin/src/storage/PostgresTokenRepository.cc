#include <oauth2/storage/PostgresTokenRepository.h>
#include <drogon/drogon.h>

#include <authforge/storage/postgres/models/Oauth2AccessTokens.h>
#include <authforge/storage/postgres/models/Oauth2RefreshTokens.h>

#include <chrono>
#include <ctime>

namespace oauth2
{

// Task 27.5: callback + DTO aliases for the new base interface; safe at namespace scope here (this
// .cc does not include IOAuth2Storage.h, so no oauth2::* clash).
using OAuth2AccessToken = ::authforge::oauth2::model::OAuth2AccessToken;
using OAuth2RefreshToken = ::authforge::oauth2::model::OAuth2RefreshToken;
using TokenIntrospection = ::authforge::oauth2::model::TokenIntrospection;
using VoidCallback = ITokenRepositoryBase::VoidCallback;
using AccessTokenCallback = ITokenRepositoryBase::AccessTokenCallback;
using RefreshTokenCallback = ITokenRepositoryBase::RefreshTokenCallback;
using TokenIntrospectionCallback = ITokenRepositoryBase::TokenIntrospectionCallback;

using namespace drogon::orm;
using namespace drogon_model::oauth2_db;

void PostgresTokenRepository::saveAccessToken(const OAuth2AccessToken &token, VoidCallback &&cb)
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
          }
        );
    }
    catch (...)
    {
        LOG_ERROR << "saveAccessToken Exception";
        if (*sharedCb)
            (*sharedCb)();
    }
}

void PostgresTokenRepository::saveTokenPair(
  const OAuth2AccessToken &at,
  const OAuth2RefreshToken &rt,
  VoidCallback &&cb
)
{
    if (!dbClientMaster_)
    {
        if (cb)
            cb();
        return;
    }
    auto sharedCb = std::make_shared<VoidCallback>(std::move(cb));

    // Bug fix: the caller's callback must not fire until the transaction
    // has actually been COMMITted. Drogon only issues "commit" to Postgres
    // when the last shared_ptr reference to the Transaction object is
    // released (see drogon::orm::TransactionImpl::~TransactionImpl), which
    // happens asynchronously, some time after the second INSERT's own
    // result callback returns. The previous implementation called the
    // caller's callback directly from that second INSERT's success
    // callback -- i.e. before COMMIT was even sent. A second connection
    // (e.g. this repository's own dbClientReader_, or a contract test using
    // a separate client) reading immediately afterwards could observe
    // pre-commit state under Postgres MVCC and see neither row. This was
    // intermittent (a race between the async COMMIT round trip and a
    // subsequent read on a different connection), which is why it only
    // reproduced under some CI runners/timings and not deterministically
    // in every environment.
    //
    // Fix: install a commit callback via newTransaction() and fire the
    // caller's callback from there (guaranteed to run only after COMMIT
    // completes). The insert error paths still invoke the caller's
    // callback directly, because Drogon automatically rolls back on error
    // and its own documentation states the commit callback "will never be
    // executed" if the transaction is rolled back.
    auto invoked = std::make_shared<bool>(false);
    auto invokeOnce = [sharedCb, invoked]() {
        if (!*invoked)
        {
            *invoked = true;
            if (*sharedCb)
                (*sharedCb)();
        }
    };

    // Use a transaction to ensure both tokens are saved atomically
    auto transPtr = dbClientMaster_->newTransaction([invokeOnce](bool committed) {
        if (!committed)
            LOG_ERROR << "saveTokenPair: transaction commit failed";
        invokeOnce();
    });

    auto refreshInsertErrorCb = [invokeOnce](const DrogonDbException &e) {
        LOG_ERROR << "saveTokenPair (refresh) failed: " << e.base().what();
        invokeOnce();
    };

    transPtr->execSqlAsync(
      "INSERT INTO oauth2_access_tokens (token, client_id, user_id, scope, expires_at, revoked) "
      "VALUES ($1, $2, $3, $4, $5, $6)",
      [transPtr, rt, refreshInsertErrorCb](const drogon::orm::Result &) {
          // Access token saved, now save refresh token. The caller's
          // callback fires from the commit callback above, not here.
          if (rt.familyId.empty())
          {
              transPtr->execSqlAsync(
                "INSERT INTO oauth2_refresh_tokens "
                "(token, access_token, client_id, user_id, scope, expires_at, revoked) "
                "VALUES ($1, $2, $3, $4, $5, $6, $7)",
                [](const drogon::orm::Result &) {},
                refreshInsertErrorCb,
                rt.token,
                rt.accessToken,
                rt.clientId,
                rt.userId,
                rt.scope,
                rt.expiresAt,
                rt.revoked
              );
          }
          else
          {
              transPtr->execSqlAsync(
                "INSERT INTO oauth2_refresh_tokens "
                "(token, access_token, client_id, user_id, scope, expires_at, revoked, family_id) "
                "VALUES ($1, $2, $3, $4, $5, $6, $7, $8)",
                [](const drogon::orm::Result &) {},
                refreshInsertErrorCb,
                rt.token,
                rt.accessToken,
                rt.clientId,
                rt.userId,
                rt.scope,
                rt.expiresAt,
                rt.revoked,
                rt.familyId
              );
          }
      },
      [invokeOnce](const DrogonDbException &e) {
          LOG_ERROR << "saveTokenPair (access) failed: " << e.base().what();
          invokeOnce();
      },
      at.token,
      at.clientId,
      at.userId,
      at.scope,
      at.expiresAt,
      at.revoked
    );
}

void PostgresTokenRepository::getAccessToken(const std::string &token, AccessTokenCallback &&cb)
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
          Criteria(Oauth2AccessTokens::Cols::_token, CompareOperator::EQ, token),
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
              LOG_DEBUG << "getAccessToken not found/error: " << e.base().what();
              (*sharedCb)(std::nullopt);
          }
        );
    }
    catch (...)
    {
        LOG_ERROR << "getAccessToken Exception";
        (*sharedCb)(std::nullopt);
    }
}

void PostgresTokenRepository::saveRefreshToken(const OAuth2RefreshToken &token, VoidCallback &&cb)
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
        if (!token.familyId.empty())
            newToken.setFamilyId(token.familyId);

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
          }
        );
    }
    catch (...)
    {
        LOG_ERROR << "saveRefreshToken Exception";
        if (*sharedCb)
            (*sharedCb)();
    }
}

void PostgresTokenRepository::getRefreshToken(const std::string &token, RefreshTokenCallback &&cb)
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
          Criteria(Oauth2RefreshTokens::Cols::_token, CompareOperator::EQ, token),
          [sharedCb](const Oauth2RefreshTokens &row) {
              OAuth2RefreshToken t;
              t.token = row.getValueOfToken();
              t.accessToken = row.getValueOfAccessToken();
              t.clientId = row.getValueOfClientId();
              t.userId = row.getValueOfUserId();
              t.scope = row.getValueOfScope();
              t.expiresAt = row.getValueOfExpiresAt();
              t.revoked = row.getValueOfRevoked();
              t.familyId = row.getValueOfFamilyId();
              (*sharedCb)(t);
          },
          [sharedCb](const DrogonDbException &e) {
              LOG_DEBUG << "getRefreshToken not found/error: " << e.base().what();
              (*sharedCb)(std::nullopt);
          }
        );
    }
    catch (...)
    {
        LOG_ERROR << "getRefreshToken Exception";
        (*sharedCb)(std::nullopt);
    }
}

void PostgresTokenRepository::revokeRefreshToken(const std::string &token, VoidCallback &&cb)
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
        Oauth2RefreshTokens updateObj;
        updateObj.setToken(token);
        updateObj.setRevoked(true);

        mapper.update(
          updateObj,
          [sharedCb, token](const size_t count) {
              LOG_DEBUG << "Revoked refresh token: " << token << ", affected rows: " << count;
              if (*sharedCb)
                  (*sharedCb)();
          },
          [sharedCb, token](const DrogonDbException &e) {
              LOG_ERROR << "Failed to revoke refresh token: " << token
                        << ", error: " << e.base().what();
              if (*sharedCb)
                  (*sharedCb)();
          }
        );
    }
    catch (...)
    {
        LOG_ERROR << "revokeRefreshToken Exception";
        if (*sharedCb)
            (*sharedCb)();
    }
}

void PostgresTokenRepository::atomicRevokeRefreshToken(
  const std::string &token,
  RefreshTokenCallback &&cb
)
{
    if (!dbClientMaster_)
    {
        cb(std::nullopt);
        return;
    }
    auto sharedCb = std::make_shared<RefreshTokenCallback>(std::move(cb));

    // Atomic CAS: UPDATE ... WHERE revoked=false RETURNING *
    dbClientMaster_->execSqlAsync(
      "UPDATE oauth2_refresh_tokens SET revoked = true "
      "WHERE token = $1 AND revoked = false "
      "RETURNING token, access_token, client_id, user_id, scope, expires_at, family_id",
      [sharedCb](const drogon::orm::Result &r) {
          if (r.empty())
          {
              // Already revoked or not found -> reuse detected
              (*sharedCb)(std::nullopt);
              return;
          }
          auto row = r[0];
          OAuth2RefreshToken rt;
          rt.token = row["token"].as<std::string>();
          rt.accessToken = row["access_token"].as<std::string>();
          rt.clientId = row["client_id"].as<std::string>();
          rt.userId = row["user_id"].as<std::string>();
          rt.scope = row["scope"].isNull() ? "" : row["scope"].as<std::string>();
          rt.expiresAt = row["expires_at"].as<int64_t>();
          rt.familyId = row["family_id"].isNull() ? "" : row["family_id"].as<std::string>();
          rt.revoked = true;
          (*sharedCb)(rt);
      },
      [sharedCb](const DrogonDbException &e) {
          LOG_ERROR << "atomicRevokeRefreshToken error: " << e.base().what();
          (*sharedCb)(std::nullopt);
      },
      token
    );
}

void PostgresTokenRepository::revokeTokenFamily(const std::string &familyId, VoidCallback &&cb)
{
    if (!dbClientMaster_ || familyId.empty())
    {
        if (cb)
            cb();
        return;
    }
    auto sharedCb = std::make_shared<VoidCallback>(std::move(cb));

    // Revoke all refresh tokens in the family
    dbClientMaster_->execSqlAsync(
      "UPDATE oauth2_refresh_tokens SET revoked = true WHERE family_id = $1",
      [sharedCb, familyId, self = shared_from_this(), this](const drogon::orm::Result &) {
          // Also revoke all associated access tokens
          dbClientMaster_->execSqlAsync(
            "UPDATE oauth2_access_tokens SET revoked = true "
            "WHERE token IN (SELECT access_token FROM oauth2_refresh_tokens WHERE family_id = $1)",
            [sharedCb, familyId](const drogon::orm::Result &) {
                LOG_WARN << "[SECURITY] Token family cascade-revoked: " << familyId;
                if (*sharedCb)
                    (*sharedCb)();
            },
            [sharedCb](const DrogonDbException &e) {
                LOG_ERROR << "revokeTokenFamily (access tokens) error: " << e.base().what();
                if (*sharedCb)
                    (*sharedCb)();
            },
            familyId
          );
      },
      [sharedCb](const DrogonDbException &e) {
          LOG_ERROR << "revokeTokenFamily error: " << e.base().what();
          if (*sharedCb)
              (*sharedCb)();
      },
      familyId
    );
}

// ========== P1: Token Introspection (RFC 7662) ==========

void PostgresTokenRepository::introspectToken(
  const std::string &token,
  TokenIntrospectionCallback &&cb
)
{
    if (!dbClientReader_)
    {
        TokenIntrospection introspection;
        introspection.active = false;
        cb(introspection);
        return;
    }

    auto sharedCb = std::make_shared<TokenIntrospectionCallback>(std::move(cb));
    int64_t now = std::time(nullptr);

    // Use raw SQL to handle both old and new database schemas
    // This query will work whether P1 columns exist or not
    // We check both access tokens and refresh tokens
    std::string sql = R"(
        SELECT token, client_id, user_id, scope, expires_at, revoked,
               COALESCE(issued_at, EXTRACT(EPOCH FROM CURRENT_TIMESTAMP)::bigint) as issued_at,
               COALESCE(issuer, 'https://oauth.example.com') as issuer,
               COALESCE(audience, '') as audience,
               COALESCE(not_before, EXTRACT(EPOCH FROM CURRENT_TIMESTAMP)::bigint) as not_before
        FROM oauth2_access_tokens
        WHERE token = $1
        UNION ALL
        SELECT token, client_id, user_id, scope, expires_at, revoked,
               EXTRACT(EPOCH FROM CURRENT_TIMESTAMP)::bigint as issued_at,
               'https://oauth.example.com' as issuer,
               '' as audience,
               EXTRACT(EPOCH FROM CURRENT_TIMESTAMP)::bigint as not_before
        FROM oauth2_refresh_tokens
        WHERE token = $1
    )";

    dbClientReader_->execSqlAsync(
      sql,
      [sharedCb, now](const Result &result) {
          TokenIntrospection introspection;

          if (result.size() == 0)
          {
              introspection.active = false;
              (*sharedCb)(introspection);
              return;
          }

          auto row = result[0];
          bool revoked = row["revoked"].as<bool>();
          int64_t expiresAt = row["expires_at"].as<int64_t>();

          if (revoked || expiresAt < now)
          {
              introspection.active = false;
              (*sharedCb)(introspection);
              return;
          }

          // Token is active, populate introspection data
          introspection.active = true;
          introspection.clientId = row["client_id"].as<std::string>();
          introspection.tokenType = "Bearer";
          introspection.exp = expiresAt;
          introspection.iat = row["issued_at"].as<int64_t>();
          introspection.iss = row["issuer"].as<std::string>();
          introspection.aud = row["audience"].as<std::string>();
          introspection.nbf = row["not_before"].as<int64_t>();
          introspection.sub = row["user_id"].as<std::string>();
          introspection.scope = row["scope"].as<std::string>();

          (*sharedCb)(introspection);
      },
      [sharedCb](const DrogonDbException &e) {
          LOG_DEBUG << "introspectToken error: " << e.base().what();
          TokenIntrospection introspection;
          introspection.active = false;
          (*sharedCb)(introspection);
      },
      token.c_str()
    );
}

void PostgresTokenRepository::incrementIntrospectCount(const std::string &token, VoidCallback &&cb)
{
    if (!dbClientMaster_)
    {
        cb();
        return;
    }

    auto sharedCb = std::make_shared<VoidCallback>(std::move(cb));

    // Try to increment introspect_count (P1 feature)
    // This will fail gracefully if column doesn't exist (P0 compatibility)
    std::string sql =
      "UPDATE oauth2_access_tokens "
      "SET introspect_count = COALESCE(introspect_count, 0) + 1 "
      "WHERE token = $1";

    dbClientMaster_->execSqlAsync(
      sql,
      [sharedCb](const Result &) { (*sharedCb)(); },
      [sharedCb](const DrogonDbException &e) {
          // Column might not exist (P0 compatibility), log and continue
          LOG_DEBUG << "incrementIntrospectCount failed (P0 compatibility): " << e.base().what();
          (*sharedCb)();
      },
      token.c_str()
    );
}

// ========== P1: Token Revocation (RFC 7009) ==========

void PostgresTokenRepository::revokeAccessToken(
  const std::string &token,
  const std::string &revokedBy,
  VoidCallback &&cb
)
{
    if (!dbClientMaster_)
    {
        cb();
        return;
    }

    auto sharedCb = std::make_shared<VoidCallback>(std::move(cb));
    int64_t now = std::time(nullptr);

    // Update token as revoked with audit trail (P1)
    // This works with both old and new schemas
    // We try to revoke in both access tokens and refresh tokens tables
    dbClientMaster_->execSqlAsync(
      "UPDATE oauth2_access_tokens SET revoked = TRUE, revoked_at = $1, revoked_by = $2 WHERE "
      "token = $3",
      [self = shared_from_this(), this, sharedCb, now, revokedBy, token](const Result &) {
          dbClientMaster_->execSqlAsync(
            "UPDATE oauth2_refresh_tokens SET revoked = TRUE, revoked_at = $1, revoked_by = $2 "
            "WHERE token = $3",
            [sharedCb](const Result &) {
                LOG_DEBUG << "Token revoked successfully (checked both tables)";
                (*sharedCb)();
            },
            [sharedCb](const DrogonDbException &e) {
                // Refresh tokens table might not have audit columns yet?
                LOG_DEBUG << "Refresh token revocation audit failed: " << e.base().what();
                (*sharedCb)();
            },
            now,
            revokedBy.c_str(),
            token.c_str()
          );
      },
      [self = shared_from_this(), this, sharedCb, now, revokedBy, token](
        const DrogonDbException &e
      ) {
          LOG_DEBUG << "Access token revocation audit failed: " << e.base().what();
          // Fallback to simple revoked = TRUE
          dbClientMaster_->execSqlAsync(
            "UPDATE oauth2_access_tokens SET revoked = TRUE WHERE token = $1",
            [self, this, sharedCb, token](const Result &) {
                dbClientMaster_->execSqlAsync(
                  "UPDATE oauth2_refresh_tokens SET revoked = TRUE WHERE token = $1",
                  [sharedCb](const Result &) { (*sharedCb)(); },
                  [sharedCb](const DrogonDbException &) { (*sharedCb)(); },
                  token.c_str()
                );
            },
            [sharedCb](const DrogonDbException &) { (*sharedCb)(); },
            token.c_str()
          );
      },
      now,
      revokedBy.c_str(),
      token.c_str()
    );
}

// ========== Cleanup ==========

void PostgresTokenRepository::purgeExpired()
{
    // Token-side slice of the original PostgresOAuth2Storage::deleteExpiredData():
    // access/refresh token sweeps + the archive_expired_tokens() call. The
    // auth-code sweep lives in PostgresGrantRepository::purgeExpired() instead
    // (see REPOSITORY_MAPPING.md #32 decision table).
    if (!dbClientMaster_)
        return;

    auto now = std::chrono::duration_cast<std::chrono::seconds>(
                 std::chrono::system_clock::now().time_since_epoch()
    )
                 .count();

    try
    {
        // Access Tokens
        Mapper<Oauth2AccessTokens> atMapper(dbClientMaster_);
        atMapper.deleteBy(
          Criteria(Oauth2AccessTokens::Cols::_expires_at, CompareOperator::LT, now),
          [](const size_t count) {
              if (count > 0)
                  LOG_INFO << "Cleaned " << count << " expired access tokens";
          },
          [](const DrogonDbException &e) {
              LOG_ERROR << "Cleanup AccessTokens Error: " << e.base().what();
          }
        );

        // Refresh Tokens
        Mapper<Oauth2RefreshTokens> rtMapper(dbClientMaster_);
        rtMapper.deleteBy(
          Criteria(Oauth2RefreshTokens::Cols::_expires_at, CompareOperator::LT, now),
          [](const size_t count) {
              if (count > 0)
                  LOG_INFO << "Cleaned " << count << " expired refresh tokens";
          },
          [](const DrogonDbException &e) {
              LOG_ERROR << "Cleanup RefreshTokens Error: " << e.base().what();
          }
        );

        // Archive old tokens (older than 30 days)
        dbClientMaster_->execSqlAsync(
          "SELECT archive_expired_tokens(30)",
          [](const drogon::orm::Result &r) {
              if (!r.empty() && r[0][0].as<int>() > 0)
              {
                  LOG_INFO << "Archived " << r[0][0].as<int>() << " expired tokens";
              }
          },
          [](const DrogonDbException &e) {
              LOG_DEBUG << "Token archival skipped (function may not exist): " << e.base().what();
          }
        );
    }
    catch (...)
    {
        LOG_ERROR << "PostgresTokenRepository::purgeExpired Exception";
    }
}

}  // namespace oauth2
