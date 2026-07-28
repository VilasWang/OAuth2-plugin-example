#include <authforge/storage/postgres/PostgresGrantRepository.h>
#include <drogon/drogon.h>

#include <authforge/storage/postgres/models/Oauth2Codes.h>

#include <chrono>

namespace authforge::storage::postgres
{

// Task 27.5: callback + DTO aliases for the new base interface; safe at namespace scope here (this
// .cc does not include IOAuth2Storage.h, so no oauth2::* clash).
using OAuth2AuthCode = ::authforge::oauth2::model::OAuth2AuthCode;
using AuthorizationTransaction = ::authforge::oauth2::model::AuthorizationTransaction;
using BoolCallback = IGrantRepositoryBase::BoolCallback;
using AuthCodeCallback = IGrantRepositoryBase::AuthCodeCallback;
using VoidCallback = IGrantRepositoryBase::VoidCallback;
using TransactionCallback = IGrantRepositoryBase::TransactionCallback;

using namespace ::drogon::orm;
using namespace drogon_model::oauth2_db;

void PostgresGrantRepository::saveAuthCode(const OAuth2AuthCode &code, VoidCallback &&cb)
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
          }
        );
    }
    catch (...)
    {
        LOG_ERROR << "saveAuthCode Exception";
        if (*sharedCb)
            (*sharedCb)();
    }
}

void PostgresGrantRepository::getAuthCode(const std::string &code, AuthCodeCallback &&cb)
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
              LOG_DEBUG << "getAuthCode not found or error: " << e.base().what();
              (*sharedCb)(std::nullopt);
          }
        );
    }
    catch (...)
    {
        LOG_ERROR << "getAuthCode Exception";
        (*sharedCb)(std::nullopt);
    }
}

void PostgresGrantRepository::markAuthCodeUsed(const std::string &code, VoidCallback &&cb)
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
          }
        );
    }
    catch (...)
    {
        LOG_ERROR << "markAuthCodeUsed Exception";
        if (*sharedCb)
            (*sharedCb)();
    }
}

void PostgresGrantRepository::consumeAuthCode(
  const std::string &code,
  const std::string &redirectUri,
  AuthCodeCallback &&cb
)
{
    if (!dbClientMaster_)
    {
        cb(std::nullopt);
        return;
    }
    auto sharedCb = std::make_shared<AuthCodeCallback>(std::move(cb));

    // Atomic CAS: UPDATE ... WHERE used=false RETURNING *
    // This prevents race conditions where two concurrent requests consume the same code
    dbClientMaster_->execSqlAsync(
      "UPDATE oauth2_codes SET used = true "
      "WHERE code = $1 AND used = false "
      "RETURNING code, client_id, user_id, scope, redirect_uri, "
      "code_challenge, code_challenge_method, expires_at",
      [sharedCb, redirectUri, code](const ::drogon::orm::Result &r) {
          if (r.empty())
          {
              LOG_DEBUG << "[SECURITY] Auth code not found or already used: " << code.substr(0, 8);
              (*sharedCb)(std::nullopt);
              return;
          }

          auto row = r[0];

          // Validate redirect_uri matches (RFC 6749 Section 4.1.3)
          std::string storedRedirectUri =
            row["redirect_uri"].isNull() ? "" : row["redirect_uri"].as<std::string>();
          if (!redirectUri.empty() && redirectUri != storedRedirectUri)
          {
              LOG_WARN << "[SECURITY] redirect_uri mismatch in token exchange. "
                       << "Expected: " << storedRedirectUri << ", Got: " << redirectUri;
              (*sharedCb)(std::nullopt);
              return;
          }

          OAuth2AuthCode c;
          c.code = row["code"].as<std::string>();
          c.clientId = row["client_id"].as<std::string>();
          c.userId = row["user_id"].isNull() ? "" : row["user_id"].as<std::string>();
          c.scope = row["scope"].isNull() ? "" : row["scope"].as<std::string>();
          c.redirectUri = storedRedirectUri;
          c.codeChallenge =
            row["code_challenge"].isNull() ? "" : row["code_challenge"].as<std::string>();
          c.codeChallengeMethod = row["code_challenge_method"].isNull()
                                    ? ""
                                    : row["code_challenge_method"].as<std::string>();
          c.expiresAt = row["expires_at"].as<int64_t>();
          c.used = true;
          (*sharedCb)(c);
      },
      [sharedCb, code](const DrogonDbException &e) {
          LOG_ERROR << "consumeAuthCode atomic SQL error: " << e.base().what();
          (*sharedCb)(std::nullopt);
      },
      code
    );
}

// ========== Authorization Transaction Operations ==========
//
// NOTE (see class header comment): these four methods are a verbatim port of
// the placeholder implementation in PostgresOAuth2Storage.cc. They do not
// persist to a real table today -- this is a pre-existing limitation, not a
// regression introduced by this split.

void PostgresGrantRepository::saveAuthorizationTransaction(
  const AuthorizationTransaction &transaction,
  BoolCallback &&cb
)
{
    auto sharedCb = std::make_shared<BoolCallback>(std::move(cb));

    try
    {
        std::string scopesJson = "[";
        for (size_t i = 0; i < transaction.requestedScopes.size(); i++)
        {
            scopesJson += "\"" + transaction.requestedScopes[i] + "\"";
            if (i < transaction.requestedScopes.size() - 1)
                scopesJson += ",";
        }
        scopesJson += "]";

        std::string validScopesJson = "[";
        for (size_t i = 0; i < transaction.validScopes.size(); i++)
        {
            validScopesJson += "\"" + transaction.validScopes[i] + "\"";
            if (i < transaction.validScopes.size() - 1)
                validScopesJson += ",";
        }
        validScopesJson += "]";

        std::string consentScopesJson = "[";
        for (size_t i = 0; i < transaction.consentRequiredScopes.size(); i++)
        {
            consentScopesJson += "\"" + transaction.consentRequiredScopes[i] + "\"";
            if (i < transaction.consentRequiredScopes.size() - 1)
                consentScopesJson += ",";
        }
        consentScopesJson += "]";

        // For now, we'll use a simple in-memory storage approach
        // In production, you'd want to create a proper
        // oauth2_authorization_transactions table
        LOG_DEBUG << "Transaction saved (in-memory): " << transaction.transactionId;
        (*sharedCb)(true);
    }
    catch (...)
    {
        LOG_ERROR << "saveAuthorizationTransaction Exception";
        (*sharedCb)(false);
    }
}

void PostgresGrantRepository::getAuthorizationTransaction(
  const std::string &transactionId,
  TransactionCallback &&cb
)
{
    auto sharedCb = std::make_shared<TransactionCallback>(std::move(cb));

    // Note: This is a placeholder implementation
    // In production, you'd query from oauth2_authorization_transactions table
    LOG_DEBUG << "getAuthorizationTransaction (in-memory): " << transactionId;
    (*sharedCb)(std::nullopt);
}

void PostgresGrantRepository::deleteAuthorizationTransaction(
  const std::string &transactionId,
  VoidCallback &&cb
)
{
    auto sharedCb = std::make_shared<VoidCallback>(std::move(cb));

    // Note: This is a placeholder implementation
    LOG_DEBUG << "deleteAuthorizationTransaction: " << transactionId;
    (*sharedCb)();
}

void PostgresGrantRepository::markTransactionConsumed(
  const std::string &transactionId,
  BoolCallback &&cb
)
{
    auto sharedCb = std::make_shared<BoolCallback>(std::move(cb));

    // Note: This is a placeholder implementation
    LOG_DEBUG << "markTransactionConsumed: " << transactionId;
    (*sharedCb)(true);
}

// ========== Cleanup ==========

void PostgresGrantRepository::purgeExpired()
{
    // Grant-side slice of the original PostgresOAuth2Storage::deleteExpiredData():
    // only the auth-code sweep (oauth2_codes). The access/refresh token sweep
    // and the archive_expired_tokens() call are token-lifecycle concerns and
    // live in PostgresTokenRepository::purgeExpired() instead (see
    // REPOSITORY_MAPPING.md #32 decision table).
    if (!dbClientMaster_)
        return;

    auto now = std::chrono::duration_cast<std::chrono::seconds>(
                 std::chrono::system_clock::now().time_since_epoch()
    )
                 .count();

    try
    {
        Mapper<Oauth2Codes> codeMapper(dbClientMaster_);
        codeMapper.deleteBy(
          Criteria(Oauth2Codes::Cols::_expires_at, CompareOperator::LT, now),
          [](const size_t count) {
              if (count > 0)
                  LOG_INFO << "Cleaned " << count << " expired auth codes";
          },
          [](const DrogonDbException &e) {
              LOG_ERROR << "Cleanup Codes Error: " << e.base().what();
          }
        );
    }
    catch (...)
    {
        LOG_ERROR << "PostgresGrantRepository::purgeExpired Exception";
    }
}

}  // namespace authforge::storage::postgres
