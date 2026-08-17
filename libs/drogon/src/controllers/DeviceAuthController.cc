#include <authforge/drogon/controllers/DeviceAuthController.h>
#include <authforge/storage/postgres/models/Oauth2DeviceCodes.h>
#include <authforge/drogon/utils/CryptoUtils.h>
#include <authforge/drogon/plugin/OAuth2Plugin.h>
#include <authforge/drogon/error/OAuth2ErrorHandler.h>
#include <authforge/drogon/error/ErrorResponder.h>
#include <authforge/drogon/observability/openapi/OpenApiGenerator.h>
#include <authforge/drogon/services/DeviceCodeService.h>
#include <authforge/oauth2/model/Client.h>
#include <drogon/drogon.h>
#include <drogon/utils/Utilities.h>
#include <chrono>
#include <optional>

namespace authforge::drogon::controllers
{

namespace
{
// Emit an Application error via the unified ErrorResponder entry point so the
// body is always an Error Envelope (Requirement 7.1 / 7.3 / 7.5). This is used
// only by the user-facing /oauth2/device/approve action; the RFC 8628 device
// authorization protocol endpoint keeps emitting RFC 6749 §5.2 error bodies via
// OAuth2ErrorHandler.
void respondError(
  const ::drogon::HttpRequestPtr &req,
  const std::shared_ptr<std::function<void(const ::drogon::HttpResponsePtr &)>> &cb,
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

struct DeviceAuthControllerDocs
{
    DeviceAuthControllerDocs()
    {
        ::authforge::drogon::observability::openapi::EndpointInfo authDocs;
        authDocs.path = "/oauth2/device_authorization";
        authDocs.method = "POST";
        authDocs.summary = "Device Authorization";
        authDocs.description = "Request device authorization.";
        authDocs.tags = {"OAuth2", "Device Flow"};
        authDocs.requiresAuth = true;
        ::authforge::drogon::observability::openapi::OpenApiGenerator::addEndpoint(authDocs);

        // The former /oauth2/device/verify GET+POST doc entries were ghosts:
        // no ADD_METHOD_TO route ever backed them (the verification page is
        // rendered by the frontend). Removed; docs must equal routes
        // (OpenAPI governance gate).
        ::authforge::drogon::observability::openapi::EndpointInfo approveDocs;
        approveDocs.path = "/oauth2/device/approve";
        approveDocs.method = "POST";
        approveDocs.summary = "Approve Device Authorization";
        approveDocs.description =
          "Admin-only approval of a pending device authorization (the user_code "
          "approval step of RFC 8628). Requires an admin Bearer token.";
        approveDocs.tags = {"OAuth2", "Device Flow"};
        approveDocs.requiresAuth = true;
        ::authforge::drogon::observability::openapi::OpenApiGenerator::addEndpoint(approveDocs);
    }
};

DeviceAuthControllerDocs docs_;

constexpr int DEVICE_CODE_LIFETIME_SECONDS = 600;  // 10 minutes
constexpr int POLLING_INTERVAL_SECONDS = 5;
constexpr const char *ALLOWED_USER_CODE_CHARS = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
constexpr int USER_CODE_LENGTH = 8;

std::string getVerificationUri()
{
    auto customConfig = ::drogon::app().getCustomConfig();
    if (
      customConfig.isMember("device_authorization") &&
      customConfig["device_authorization"].isMember("verification_uri")
    )
    {
        return customConfig["device_authorization"]["verification_uri"].asString();
    }
    return "http://localhost:5555/oauth2/device";
}

// F-015 (RFC 8628 §3.2.1 defers to RFC 6749 §3.2.1): the device
// authorization endpoint MUST authenticate CONFIDENTIAL clients. Extract
// credentials the same way TokenEndpointController::extractClientCredentials
// does (HTTP Basic preferred, POST body fallback).
struct DeviceClientCredentials
{
    std::string clientId;
    std::string clientSecret;
    std::string authScheme;
};

DeviceClientCredentials extractDeviceClientCredentials(const ::drogon::HttpRequestPtr &req)
{
    DeviceClientCredentials creds;
    auto authHeader = req->getHeader("Authorization");
    if (!authHeader.empty() && authHeader.find("Basic ") == 0)
    {
        creds.authScheme = "Basic";
        try
        {
            auto decoded = ::drogon::utils::base64Decode(authHeader.substr(6));
            auto colonPos = decoded.find(':');
            if (colonPos != std::string::npos)
            {
                creds.clientId = decoded.substr(0, colonPos);
                creds.clientSecret = decoded.substr(colonPos + 1);
            }
        }
        catch (...)
        {
            LOG_ERROR << "Failed to decode Basic Auth header";
        }
    }
    else
    {
        creds.clientId = req->getParameter("client_id");
        creds.clientSecret = req->getParameter("client_secret");
    }
    return creds;
}
}  // namespace

OAuth2Plugin *DeviceAuthController::resolvePlugin() const
{
    return plugin_ ? plugin_ : ::drogon::app().getPlugin<OAuth2Plugin>();
}

std::string DeviceAuthController::generateUserCode()
{
    const std::string chars = ALLOWED_USER_CODE_CHARS;
    const size_t charsLen = chars.length();

    std::vector<unsigned char> randomBytes(USER_CODE_LENGTH);
    if (!::drogon::utils::secureRandomBytes(randomBytes.data(), USER_CODE_LENGTH))
    {
        // Fallback: use UUID-based randomness
        auto uuid = ::drogon::utils::getUuid();
        std::string code;
        for (int i = 0; i < USER_CODE_LENGTH && i < static_cast<int>(uuid.size()); ++i)
        {
            code += chars[static_cast<unsigned char>(uuid[i]) % charsLen];
        }
        return code;
    }

    std::string code;
    code.reserve(USER_CODE_LENGTH);
    for (int i = 0; i < USER_CODE_LENGTH; ++i)
    {
        code += chars[randomBytes[i] % charsLen];
    }
    return code;
}

void DeviceAuthController::deviceAuthorization(
  const ::drogon::HttpRequestPtr &req,
  std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
)
{
    LOG_DEBUG << "Device authorization request received";

    // Extract parameters. F-015: credentials come from Basic header or body
    // (client_id may be supplied either way).
    auto credentials = extractDeviceClientCredentials(req);
    std::string clientId = credentials.clientId;
    std::string clientSecret = credentials.clientSecret;
    std::string scope = req->getParameter("scope");

    if (clientId.empty())
    {
        ::authforge::common::error::OAuth2ErrorHandler::sendErrorResponse(
          std::move(callback), "invalid_request", "client_id is required"
        );
        return;
    }

    // Validate client exists
    auto plugin = resolvePlugin();
    if (!plugin)
    {
        ::authforge::common::error::OAuth2ErrorHandler::sendErrorResponse(
          std::move(callback), "server_error", "OAuth2 plugin not available"
        );
        return;
    }

    auto sharedCb =
      std::make_shared<std::function<void(const ::drogon::HttpResponsePtr &)>>(std::move(callback));

    // F-015 (RFC 8628 §3.2.1 / RFC 6749 §3.2.1): authenticate the client.
    // Previously this called validateClient(clientId, ""), which accepted any
    // existing client without a secret. Branch on client_type instead:
    //   CONFIDENTIAL -> require + validate client_secret
    //   PUBLIC       -> client_id existence check only
    plugin->getClient(
      clientId,
      [plugin, clientId, clientSecret, scope, sharedCb](
        std::optional<authforge::oauth2::model::OAuth2Client> client
      ) {
          if (!client)
          {
              ::authforge::common::error::OAuth2ErrorHandler::sendErrorResponse(
                std::move(*sharedCb), "invalid_client", "Unknown client_id"
              );
              return;
          }

          auto proceedDeviceAuth = [clientId, scope, sharedCb]() {
              deviceAuthorizationInner(clientId, scope, sharedCb);
          };

          if (
            client->clientType == authforge::oauth2::model::ClientType::CONFIDENTIAL
          )
          {
              if (clientSecret.empty())
              {
                  ::authforge::common::error::OAuth2ErrorHandler::sendErrorResponse(
                    std::move(*sharedCb),
                    "invalid_client",
                    "Client authentication required for device authorization"
                  );
                  return;
              }
              plugin->validateClient(
                clientId, clientSecret, [proceedDeviceAuth, sharedCb](bool valid) {
                    if (!valid)
                    {
                        ::authforge::common::error::OAuth2ErrorHandler::sendErrorResponse(
                          std::move(*sharedCb), "invalid_client", "Client authentication failed"
                        );
                        return;
                    }
                    proceedDeviceAuth();
                }
              );
              return;
          }

          // PUBLIC client: existence verified via getClient above.
          proceedDeviceAuth();
      }
    );
}

void DeviceAuthController::deviceAuthorizationInner(
  const std::string &clientId,
  const std::string &scope,
  const std::shared_ptr<std::function<void(const ::drogon::HttpResponsePtr &)>> &sharedCb
)
{
        // Generate device_code and user_code
        std::string deviceCode = ::authforge::drogon::utils::generateSecureToken();
        std::string deviceCodeHash = ::authforge::drogon::utils::hashToken(deviceCode);
        std::string userCode = generateUserCode();

        auto now = std::chrono::duration_cast<std::chrono::seconds>(
                     std::chrono::system_clock::now().time_since_epoch()
        )
                     .count();
        int64_t expiresAt = now + DEVICE_CODE_LIFETIME_SECONDS;

        // Store in database
        auto dbClient = ::drogon::app().getDbClient();
        if (!dbClient)
        {
            ::authforge::common::error::OAuth2ErrorHandler::sendErrorResponse(
              std::move(*sharedCb), "server_error", "Database not available"
            );
            return;
        }

        ::authforge::drogon::services::DeviceCodeService::createDeviceCode(
          deviceCodeHash,
          userCode,
          clientId,
          scope,
          expiresAt,
          POLLING_INTERVAL_SECONDS,
          dbClient,
          [deviceCode, userCode, sharedCb](bool success) {
              if (!success)
              {
                  ::authforge::common::error::OAuth2ErrorHandler::sendErrorResponse(
                    std::move(*sharedCb), "server_error", "Failed to store device authorization"
                  );
                  return;
              }
              // Success - return device authorization response
              Json::Value response;
              response["device_code"] = deviceCode;
              response["user_code"] = userCode;
              response["verification_uri"] = getVerificationUri();
              response["expires_in"] = DEVICE_CODE_LIFETIME_SECONDS;
              response["interval"] = POLLING_INTERVAL_SECONDS;

              auto resp = ::drogon::HttpResponse::newHttpJsonResponse(response);
              resp->setStatusCode(::drogon::k200OK);
              (*sharedCb)(resp);
          }
        );
}

void DeviceAuthController::approveDevice(
  const ::drogon::HttpRequestPtr &req,
  std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
)
{
    LOG_DEBUG << "Device approval request received";

    // Extract parameters
    std::string userCode = req->getParameter("user_code");
    std::string userId = req->getParameter("user_id");

    // /oauth2/device/approve is a user-facing approval action (admin-only), not a
    // standardized RFC 8628 protocol endpoint, so its errors are emitted as JSON
    // Error Envelopes via the unified entry point (Requirement 7.1 / 7.3 / 7.5).
    auto sharedCb =
      std::make_shared<std::function<void(const ::drogon::HttpResponsePtr &)>>(std::move(callback));

    if (userCode.empty())
    {
        respondError(
          req, sharedCb, "VALIDATION_MISSING_REQUIRED_FIELD", "approveDevice: user_code is required"
        );
        return;
    }

    if (userId.empty())
    {
        respondError(
          req, sharedCb, "VALIDATION_MISSING_REQUIRED_FIELD", "approveDevice: user_id is required"
        );
        return;
    }

    auto dbClient = ::drogon::app().getDbClient();
    if (!dbClient)
    {
        respondError(req, sharedCb, "DB_CONNECTION_ERROR", "approveDevice: database not available");
        return;
    }

    // Task B5: replaced raw UPDATE SQL with DeviceCodeService
    ::authforge::drogon::services::DeviceCodeService::findByUserCode(
      userCode,
      dbClient,
      [sharedCb, userCode, req, userId, dbClient](
        std::shared_ptr<::drogon_model::oauth2_db::Oauth2DeviceCodes> code
      ) {
          if (!code)
          {
              respondError(
                req, sharedCb, "VALIDATION_DEVICE_CODE_INVALID", "approveDevice: invalid user_code"
              );
              return;
          }
          // Check that the code is still pending
          auto status = code->getValueOfStatus();
          if (status != "pending" && !status.empty())
          {
              respondError(
                req,
                sharedCb,
                "VALIDATION_DEVICE_CODE_INVALID",
                "approveDevice: user_code already processed"
              );
              return;
          }
          // Check expiration
          auto now = std::chrono::duration_cast<std::chrono::seconds>(
                       std::chrono::system_clock::now().time_since_epoch()
          )
                       .count();
          if (now >= code->getValueOfExpiresAt())
          {
              respondError(
                req, sharedCb, "VALIDATION_DEVICE_CODE_INVALID", "approveDevice: user_code expired"
              );
              return;
          }

          ::authforge::drogon::services::DeviceCodeService::markApproved(
            code->getValueOfDeviceCodeHash(),
            userId,
            dbClient,
            [sharedCb, userCode, req](bool success) {
                if (!success)
                {
                    respondError(
                      req,
                      sharedCb,
                      "VALIDATION_DEVICE_CODE_INVALID",
                      "approveDevice: failed to approve device code"
                    );
                    return;
                }

                Json::Value response;
                response["status"] = "approved";
                response["user_code"] = userCode;

                auto resp = ::drogon::HttpResponse::newHttpJsonResponse(response);
                resp->setStatusCode(::drogon::k200OK);
                (*sharedCb)(resp);
            }
          );
      }
    );
}

}  // namespace authforge::drogon::controllers
