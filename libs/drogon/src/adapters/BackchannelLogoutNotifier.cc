#include <fulla/drogon/adapters/BackchannelLogoutNotifier.h>

#include <fulla/common/observability/AuditEvent.h>
#include <fulla/oauth2/protocol/LogoutToken.h>
#include <fulla/storage/postgres/models/Oauth2AccessTokens.h>
#include <fulla/storage/postgres/models/Oauth2Clients.h>

#include <drogon/drogon.h>

#include <cstdint>
#include <ctime>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace fulla::drogon::adapters
{

// Namespace-visibility trap (see ClientManagementService.cc's identical note):
// inside fulla::drogon::adapters a bare `drogon::` resolves to
// fulla::drogon first. Use ::drogon:: / ::drogon_model:: globally, then
// bring the orm + model names in for the Mapper/Criteria bodies below.
using namespace ::drogon::orm;
using namespace ::drogon_model::fulla_db;

namespace
{

void recordBackchannelAudit(
  const std::shared_ptr<fulla::common::ports::IAuditSink> &sink,
  const std::string &subject,
  const std::string &clientId,
  const std::string &uri,
  bool ok,
  int httpStatus)
{
    if (!sink)
        return;
    fulla::common::observability::AuditEvent ev;
    ev.actorType = "system";
    ev.actorId = subject;
    ev.action = "backchannel_logout";
    ev.targetType = "client";
    ev.targetId = clientId;
    ev.outcome = ok ? "success" : "failure";
    ev.details["logout_uri"] = uri;
    ev.details["http_status"] = httpStatus;
    sink->record(ev);
}

}  // namespace

BackchannelLogoutNotifier::BackchannelLogoutNotifier(
  DbClientPtr dbClient,
  std::shared_ptr<const fulla::oauth2::JwkManager> jwkManager,
  std::string issuer,
  std::shared_ptr<fulla::identity::IOAuthHttpClient> httpClient,
  std::shared_ptr<fulla::common::ports::IAuditSink> auditSink,
  int tokenTtlSeconds)
  : dbClient_(std::move(dbClient)),
    jwkManager_(std::move(jwkManager)),
    issuer_(std::move(issuer)),
    httpClient_(std::move(httpClient)),
    auditSink_(std::move(auditSink)),
    tokenTtlSeconds_(tokenTtlSeconds)
{
}

void BackchannelLogoutNotifier::notify(
  const std::string &userId,
  std::function<void()> &&callback)
{
    auto sharedCb = std::make_shared<std::function<void()>>(std::move(callback));

    // Without a DB or signing key or HTTP transport there is nothing to fan
    // out -- just complete so the logout flow is never wedged by a missing dep.
    if (!dbClient_ || !jwkManager_ || !httpClient_)
    {
        (*sharedCb)();
        return;
    }

    auto self = shared_from_this();
    const std::int64_t now = static_cast<std::int64_t>(std::time(nullptr));

    // Query 1: active sessions for this user. "Active" mirrors
    // TokenManagementService::listTokens -- expires_at > now AND
    // (revoked = false OR revoked IS NULL) -- evaluated in C++ (app clock,
    // NTP-synced in practice) so the query stays pure-Criteria (no raw SQL).
    Criteria active =
      Criteria(Oauth2AccessTokens::Cols::_expires_at, CompareOperator::GT, now) &&
      (Criteria(Oauth2AccessTokens::Cols::_revoked, CompareOperator::EQ, false) ||
       Criteria(Oauth2AccessTokens::Cols::_revoked, CompareOperator::IsNull));
    active =
      active && Criteria(Oauth2AccessTokens::Cols::_user_id, CompareOperator::EQ, userId);

    try
    {
        Mapper<Oauth2AccessTokens> tokenMapper(dbClient_);
        tokenMapper.findBy(
          active,
          [self, sharedCb, userId](const std::vector<Oauth2AccessTokens> &tokenRows) {
              // Distinct client_ids across the user's active sessions.
              std::set<std::string> clientIds;
              for (const auto &row : tokenRows)
                  clientIds.insert(row.getValueOfClientId());

              if (clientIds.empty())
              {
                  (*sharedCb)();
                  return;
              }
              std::vector<std::string> clientIdVec(clientIds.begin(), clientIds.end());

              // Query 2: load those clients' backchannel_logout_uri. Own
              // try-catch -- this Mapper is constructed inside an async cb, so
              // the outer guard does not reach it (db-operations.md 要求 1).
              try
              {
                  Mapper<Oauth2Clients> clientMapper(self->dbClient_);
                  clientMapper.findBy(
                    Criteria(Oauth2Clients::Cols::_client_id, CompareOperator::In, clientIdVec),
                    [self, sharedCb, userId](const std::vector<Oauth2Clients> &clientRows) {
                        std::vector<BackchannelRpTarget> targets;
                        for (const auto &c : clientRows)
                        {
                            const auto &uri = c.getValueOfBackchannelLogoutUri();
                            if (!uri.empty())
                                targets.push_back({c.getValueOfClientId(), uri});
                        }
                        self->dispatch(
                          userId, std::move(targets),
                          [sharedCb]() { (*sharedCb)(); });
                    },
                    [sharedCb](const DrogonDbException &) {
                        // Client lookup failed -- best-effort: complete without
                        // notifying (a transient DB error must not wedge logout).
                        (*sharedCb)();
                    });
              }
              catch (const std::exception &)
              {
                  (*sharedCb)();
              }
          },
          [sharedCb](const DrogonDbException &) {
              // Token lookup failed -- complete (db-operations.md 要求 2: a
              // failure must reach the callback, not just LOG+return).
              (*sharedCb)();
          });
    }
    catch (const std::exception &)
    {
        // Mapper construction threw -- complete (same rule as above).
        (*sharedCb)();
    }
}

void BackchannelLogoutNotifier::dispatch(
  const std::string &subject,
  const std::vector<BackchannelRpTarget> &targets,
  std::function<void()> &&completion)
{
    auto sharedCompletion = std::make_shared<std::function<void()>>(std::move(completion));

    if (!jwkManager_ || !httpClient_)
    {
        (*sharedCompletion)();
        return;
    }

    const std::int64_t now = static_cast<std::int64_t>(std::time(nullptr));
    const auto auditSink = auditSink_;  // copy the shared_ptr for capture

    for (const auto &target : targets)
    {
        if (target.backchannelLogoutUri.empty())
            continue;

        const auto jti = fulla::oauth2::protocol::generateJti();
        auto claims = fulla::oauth2::protocol::buildLogoutTokenClaims(
          issuer_, subject, target.clientId, now, tokenTtlSeconds_, jti);
        const std::string jwt = jwkManager_->signJwt(claims);
        if (jwt.empty())
        {
            // Signing failed (e.g. key not initialized) -- audit and skip.
            recordBackchannelAudit(
              auditSink, subject, target.clientId, target.backchannelLogoutUri, false, 0);
            continue;
        }

        const std::string clientId = target.clientId;
        const std::string uri = target.backchannelLogoutUri;
        std::vector<std::pair<std::string, std::string>> params;
        params.emplace_back("logout_token", jwt);

        // Fire-and-forget POST. The completion callback captures only shared
        // state (the audit sink copy) -- never this -- so it is safe even if
        // it runs after this call returns (real async transport).
        httpClient_->postForm(
          uri,
          params,
          [auditSink, subject, clientId, uri](fulla::identity::OAuthHttpResult result) {
              const bool ok = result.transportOk && result.statusCode == 200;
              recordBackchannelAudit(auditSink, subject, clientId, uri, ok, result.statusCode);
          });
    }

    // Dispatch is done (POSTs are either completed, for the synchronous fake,
    // or in flight, for the real async transport). Complete now either way --
    // logout must not wait on RP round-trips.
    (*sharedCompletion)();
}

}  // namespace fulla::drogon::adapters
