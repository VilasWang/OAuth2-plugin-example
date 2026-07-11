#include <authforge/drogon/controllers/DeviceAuthController.h>
#include <oauth2/utils/CryptoUtils.h>
#include <oauth2/plugin/OAuth2Plugin.h>
#include <oauth2/error/OAuth2ErrorHandler.h>
#include <oauth2/error/ErrorResponder.h>
#include <authforge/drogon/observability/openapi/OpenApiGenerator.h>
#include <drogon/drogon.h>
#include <drogon/utils/Utilities.h>
#include <chrono>

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
    ::common::error::ErrorResponder::respond(
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

        ::authforge::drogon::observability::openapi::EndpointInfo verifyGetDocs;
        verifyGetDocs.path = "/oauth2/device/verify";
        verifyGetDocs.method = "GET";
        verifyGetDocs.summary = "Verify Device (GET)";
        verifyGetDocs.description = "Display device verification page.";
        verifyGetDocs.tags = {"OAuth2", "Device Flow"};
        verifyGetDocs.requiresAuth = false;
        ::authforge::drogon::observability::openapi::OpenApiGenerator::addEndpoint(verifyGetDocs);

        ::authforge::drogon::observability::openapi::EndpointInfo verifyPostDocs;
        verifyPostDocs.path = "/oauth2/device/verify";
        verifyPostDocs.method = "POST";
        verifyPostDocs.summary = "Verify Device (POST)";
        verifyPostDocs.description = "Submit device verification code.";
        verifyPostDocs.tags = {"OAuth2", "Device Flow"};
        verifyPostDocs.requiresAuth = false;
        ::authforge::drogon::observability::openapi::OpenApiGenerator::addEndpoint(verifyPostDocs);
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

    // Extract parameters
    std::string clientId = req->getParameter("client_id");
    std::string scope = req->getParameter("scope");

    if (clientId.empty())
    {
        ::common::error::OAuth2ErrorHandler::sendErrorResponse(
          std::move(callback), "invalid_request", "client_id is required"
        );
        return;
    }

    // Validate client exists
    auto plugin = resolvePlugin();
    if (!plugin)
    {
        ::common::error::OAuth2ErrorHandler::sendErrorResponse(
          std::move(callback), "server_error", "OAuth2 plugin not available"
        );
        return;
    }

    auto sharedCb =
      std::make_shared<std::function<void(const ::drogon::HttpResponsePtr &)>>(std::move(callback));

    plugin->validateClient(clientId, "", [plugin, clientId, scope, sharedCb](bool valid) {
        if (!valid)
        {
            ::common::error::OAuth2ErrorHandler::sendErrorResponse(
              std::move(*sharedCb), "invalid_client", "Unknown client_id"
            );
            return;
        }

        // Generate device_code and user_code
        std::string deviceCode = ::oauth2::utils::generateSecureToken();
        std::string deviceCodeHash = ::oauth2::utils::hashToken(deviceCode);
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
            ::common::error::OAuth2ErrorHandler::sendErrorResponse(
              std::move(*sharedCb), "server_error", "Database not available"
            );
            return;
        }

        dbClient->execSqlAsync(
          "INSERT INTO oauth2_device_codes "
          "(device_code_hash, user_code, client_id, scope, status, expires_at, interval_seconds) "
          "VALUES ($1, $2, $3, $4, 'pending', $5, $6)",
          [deviceCode, userCode, expiresAt, sharedCb](const ::drogon::orm::Result &) {
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
          },
          [sharedCb](const ::drogon::orm::DrogonDbException &e) {
              LOG_ERROR << "Failed to store device code: " << e.base().what();
              ::common::error::OAuth2ErrorHandler::sendErrorResponse(
                std::move(*sharedCb), "server_error", "Failed to store device authorization"
              );
          },
          deviceCodeHash,
          userCode,
          clientId,
          scope,
          expiresAt,
          POLLING_INTERVAL_SECONDS
        );
    });
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

    auto now = std::chrono::duration_cast<std::chrono::seconds>(
                 std::chrono::system_clock::now().time_since_epoch()
    )
                 .count();

    // Update device code status to approved
    dbClient->execSqlAsync(
      "UPDATE oauth2_device_codes SET status = 'approved', user_id = $1 "
      "WHERE user_code = $2 AND status = 'pending' AND expires_at > $3",
      [sharedCb, userCode, req](const ::drogon::orm::Result &result) {
          if (result.affectedRows() == 0)
          {
              respondError(
                req,
                sharedCb,
                "VALIDATION_DEVICE_CODE_INVALID",
                "approveDevice: invalid, expired, or already processed user_code"
              );
              return;
          }

          Json::Value response;
          response["status"] = "approved";
          response["user_code"] = userCode;

          auto resp = ::drogon::HttpResponse::newHttpJsonResponse(response);
          resp->setStatusCode(::drogon::k200OK);
          (*sharedCb)(resp);
      },
      [sharedCb, req](const ::drogon::orm::DrogonDbException &e) {
          LOG_ERROR << "Failed to approve device code: " << e.base().what();
          respondError(
            req,
            sharedCb,
            "DB_QUERY_ERROR",
            std::string("approveDevice: failed to approve device: ") + e.base().what()
          );
      },
      userId,
      userCode,
      now
    );
}

}  // namespace authforge::drogon::controllers
