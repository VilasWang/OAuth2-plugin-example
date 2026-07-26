#include <authforge/drogon/services/ClientRegistrationService.h>

#include <oauth2/adapters/DrogonAuditSink.h>
#include <authforge/storage/postgres/models/Oauth2Clients.h>
#include <oauth2/error/ErrorResponder.h>
#include <oauth2/plugin/OAuth2Plugin.h>
#include <oauth2/utils/CryptoUtils.h>

#include <drogon/drogon.h>

#include <chrono>

namespace authforge::drogon::services
{

namespace
{
void respondError(
  const ::drogon::HttpRequestPtr &req,
  const ClientRegistrationService::ResponseCallback &cb,
  std::string code,
  std::string detailForLog = ""
)
{
    ::authforge::common::error::ErrorResponder::respond(
      req,
      [cb](const ::drogon::HttpResponsePtr &r) { (*cb)(r); },
      std::move(code),
      std::move(detailForLog)
    );
}
}  // namespace

using namespace ::drogon::orm;
using namespace ::drogon_model::oauth2_db;

void ClientRegistrationService::registerClient(
  const ::drogon::HttpRequestPtr &req,
  ResponseCallback sharedCb
)
{
    // Parse request body
    auto jsonBody = req->getJsonObject();
    if (!jsonBody)
    {
        respondError(
          req,
          sharedCb,
          "VALIDATION_INVALID_INPUT",
          "registerClient: request body must be valid JSON"
        );
        return;
    }

    std::string clientName = (*jsonBody).get("client_name", "").asString();
    std::string clientType = (*jsonBody).get("client_type", "CONFIDENTIAL").asString();
    std::string tokenEndpointAuthMethod =
      (*jsonBody).get("token_endpoint_auth_method", "client_secret_basic").asString();

    if (clientName.empty())
    {
        respondError(
          req,
          sharedCb,
          "VALIDATION_MISSING_REQUIRED_FIELD",
          "registerClient: client_name is required"
        );
        return;
    }

    if (clientType != "CONFIDENTIAL" && clientType != "PUBLIC")
    {
        respondError(
          req,
          sharedCb,
          "VALIDATION_FORMAT_ERROR",
          "registerClient: client_type must be CONFIDENTIAL or PUBLIC"
        );
        return;
    }

    std::string redirectUris;
    Json::Value redirectUrisArray(Json::arrayValue);
    if (jsonBody->isMember("redirect_uris") && (*jsonBody)["redirect_uris"].isArray())
    {
        const auto &uris = (*jsonBody)["redirect_uris"];
        for (Json::ArrayIndex i = 0; i < uris.size(); ++i)
        {
            if (i > 0)
                redirectUris += ",";
            redirectUris += uris[i].asString();
            redirectUrisArray.append(uris[i].asString());
        }
    }

    if (redirectUris.empty() && clientType == "CONFIDENTIAL")
    {
        respondError(
          req,
          sharedCb,
          "VALIDATION_MISSING_REQUIRED_FIELD",
          "registerClient: redirect_uris is required for confidential clients"
        );
        return;
    }

    std::string allowedGrantTypes;
    Json::Value grantTypesArray(Json::arrayValue);
    if (jsonBody->isMember("grant_types") && (*jsonBody)["grant_types"].isArray())
    {
        const auto &grants = (*jsonBody)["grant_types"];
        for (Json::ArrayIndex i = 0; i < grants.size(); ++i)
        {
            std::string grantType = grants[i].asString();
            if (
              grantType != "authorization_code" && grantType != "refresh_token" &&
              grantType != "client_credentials"
            )
            {
                respondError(
                  req,
                  sharedCb,
                  "VALIDATION_FORMAT_ERROR",
                  "registerClient: unsupported grant_type: " + grantType +
                    ". Allowed: authorization_code, refresh_token, client_credentials"
                );
                return;
            }
            if (i > 0)
                allowedGrantTypes += ",";
            allowedGrantTypes += grantType;
            grantTypesArray.append(grantType);
        }
    }
    else
    {
        allowedGrantTypes = "authorization_code";
        grantTypesArray.append("authorization_code");
    }

    std::string clientId = ::drogon::utils::getUuid();
    std::string clientSecret = ::authforge::drogon::utils::generateSecureToken();
    std::string secretHash = ::authforge::drogon::utils::hashToken(clientSecret);
    std::string salt = ::drogon::utils::getUuid().substr(0, 36);

    auto now = std::chrono::system_clock::now();
    auto issuedAt =
      std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();

    try
    {
        auto db = ::drogon::app().getDbClient();

        Oauth2Clients client;
        client.setClientId(clientId);
        client.setClientType(clientType);
        client.setClientSecret(secretHash);
        client.setSalt(salt);
        client.setName(clientName);
        client.setRedirectUris(redirectUris);
        client.setAllowedGrantTypes(allowedGrantTypes);

        Mapper<Oauth2Clients> mapper(db);
        mapper.insert(
          client,
          [sharedCb,
           clientId,
           clientSecret,
           clientName,
           redirectUrisArray,
           grantTypesArray,
           issuedAt,
           tokenEndpointAuthMethod,
           req](const Oauth2Clients &) {
              Json::Value json;
              json["client_id"] = clientId;
              json["client_secret"] = clientSecret;
              json["client_name"] = clientName;
              json["redirect_uris"] = redirectUrisArray;
              json["grant_types"] = grantTypesArray;
              json["token_endpoint_auth_method"] = tokenEndpointAuthMethod;
              json["client_id_issued_at"] = static_cast<Json::Int64>(issuedAt);
              json["client_secret_expires_at"] = 0;

              auto resp = ::drogon::HttpResponse::newHttpJsonResponse(json);
              resp->setStatusCode(::drogon::k201Created);

              ::authforge::drogon::adapters::DrogonAuditSink::logFromRequest(
                ::drogon::app().getPlugin<::OAuth2Plugin>()->getAuditSink(),
                "client_registered",
                "success",
                req,
                "",
                "client",
                clientId
              );

              (*sharedCb)(resp);
          },
          [sharedCb, req](const DrogonDbException &e) {
              ::authforge::drogon::adapters::DrogonAuditSink::logFromRequest(
                ::drogon::app().getPlugin<::OAuth2Plugin>()->getAuditSink(),
                "client_registered",
                "failure",
                req,
                "",
                "client",
                "",
                Json::Value(e.base().what())
              );
              respondError(
                req,
                sharedCb,
                "DB_QUERY_ERROR",
                std::string("registerClient: failed to register client: ") + e.base().what()
              );
          }
        );
    }
    catch (...)
    {
        respondError(
          req, sharedCb, "DB_CONNECTION_ERROR", "registerClient: database connection error"
        );
    }
}

}  // namespace authforge::drogon::services
