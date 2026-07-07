#include <drogon/drogon.h>
#include <drogon/plugins/Hodor.h>
#include <drogon/utils/Utilities.h>
#include <vector>
#include <string>
#include <algorithm>
#include <json/json.h>
#include <sstream>
#include <oauth2/config/ConfigManager.h>
#include <oauth2/observability/openapi/OpenApiGenerator.h>
#include <oauth2/controllers/OAuth2StandardController.h>
#include <oauth2/filters/OAuth2AuthFilter.h>
#include <oauth2/error/ErrorCatalog.h>
#include <oauth2/error/ErrorResponder.h>
#include <oauth2/error/ErrorTypes.h>
#include <oauth2/error/OAuth2ErrorHandler.h>
#include <oauth2/error/RequestId.h>
#include "controllers/SessionController.h"
#include "SchemaManager.h"

using namespace drogon;

// Helper to parse JSON (replaces deprecated Json::Reader)
static bool parseJsonString(std::istream &stream, Json::Value &json)
{
    Json::CharReaderBuilder builder;
    std::string errs;
    return Json::parseFromStream(builder, stream, &json, &errs);
}

// Helper to serialize JSON to string (replaces deprecated Json::StyledWriter)
static std::string jsonToStyledString(const Json::Value &json)
{
    Json::StreamWriterBuilder builder;
    return Json::writeString(builder, json);
}

#include <filesystem>
#include <fstream>
#include <iostream>
#include <cstdlib>

// Helper to create log directory from config
void createLogDirFromConfig(const std::string &configPath)
{
    std::ifstream configFile(configPath);
    if (!configFile.is_open())
        return;

    Json::Value root;
    if (parseJsonString(configFile, root))
    {
        const auto &logConfig = root["app"]["log"];
        if (!logConfig.isNull())
        {
            std::string logPath = logConfig.get("log_path", "").asString();
            if (!logPath.empty())
            {
                try
                {
                    if (!std::filesystem::exists(logPath))
                    {
                        std::filesystem::create_directories(logPath);
                        LOG_INFO << "Created log directory: " << logPath;
                    }
                }
                catch (const std::exception &e)
                {
                    LOG_ERROR << "Failed to create log directory: " << e.what();
                }
            }
        }
    }
}

void setupCors()
{
    // Define the whitelist check logic - STRICT MODE: no wildcards
    auto isAllowed = [](const std::string &origin) -> bool {
        if (origin.empty())
            return false;

        const auto &customConfig = drogon::app().getCustomConfig();
        const auto &allowOrigins = customConfig["cors"]["allow_origins"];

        if (allowOrigins.isArray())
        {
            for (const auto &allowed : allowOrigins)
            {
                auto allowedStr = allowed.asString();
                // SECURITY: Only exact match allowed, no wildcards
                // This prevents CSRF attacks from arbitrary origins
                if (allowedStr == origin)
                    return true;
            }
        }
        return false;
    };

    // Register sync advice to handle CORS preflight (OPTIONS) requests
    drogon::app().registerSyncAdvice(
      [isAllowed](const drogon::HttpRequestPtr &req) -> drogon::HttpResponsePtr {
          if (req->method() == drogon::HttpMethod::Options)
          {
              const auto &origin = req->getHeader("Origin");
              if (isAllowed(origin))
              {
                  auto resp = drogon::HttpResponse::newHttpResponse();
                  resp->addHeader("Access-Control-Allow-Origin", origin);

                  const auto &requestMethod = req->getHeader("Access-Control-Request-Method");
                  if (!requestMethod.empty())
                  {
                      resp->addHeader("Access-Control-Allow-Methods", requestMethod);
                  }

                  resp->addHeader("Access-Control-Allow-Credentials", "true");

                  const auto &requestHeaders = req->getHeader("Access-Control-Request-Headers");
                  if (!requestHeaders.empty())
                  {
                      resp->addHeader("Access-Control-Allow-Headers", requestHeaders);
                  }
                  return resp;
              }
              // SECURITY: Reject unauthorized preflight requests with 403
              auto resp = drogon::HttpResponse::newHttpResponse();
              resp->setStatusCode(drogon::k403Forbidden);
              return resp;
          }
          return {};
      }
    );

    // Register post-handling advice to add CORS headers to all responses
    drogon::app().registerPostHandlingAdvice(
      [isAllowed](const drogon::HttpRequestPtr &req, const drogon::HttpResponsePtr &resp) {
          const auto &origin = req->getHeader("Origin");
          if (isAllowed(origin))
          {
              resp->addHeader("Access-Control-Allow-Origin", origin);
              resp->addHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
              resp->addHeader("Access-Control-Allow-Headers", "Content-Type, Authorization");
              resp->addHeader("Access-Control-Allow-Credentials", "true");
          }
      }
    );
}

// Load configuration with ConfigManager
Json::Value loadConfiguration(const std::string &configPath)
{
    Json::Value config;

    if (!common::config::ConfigManager::load(configPath, config))
    {
        LOG_FATAL << "Failed to load configuration from: " << configPath;
        exit(1);
    }

    std::string validationError;
    if (!common::config::ConfigManager::validate(config, validationError))
    {
        LOG_FATAL << "Configuration validation failed: " << validationError;
        exit(1);
    }

    LOG_INFO << "Configuration loaded successfully";
    return config;
}

int main()
{
    // Set HTTP listener address and port
    // drogon::app().addListener("0.0.0.0", 5555);

    // Search for config.json
    std::string configPath = "./config.json";
    if (!std::filesystem::exists(configPath))
        configPath = "../config.json";
    if (!std::filesystem::exists(configPath))
        configPath = "../../../config.json";

    if (!std::filesystem::exists(configPath))
    {
        std::cerr << "WARNING: config.json not found during pre-start check." << std::endl;
        return 1;
    }
    // Ensure log directory exists
    createLogDirFromConfig(configPath);

    // Load config from file with environment variable overrides
    Json::Value config = loadConfiguration(configPath);
    drogon::app().loadConfigJson(config);

    // Log configuration values for startup information
    LOG_INFO
      << "Database host: "
      << common::config::ConfigManager::get<std::string>(config, "db_clients.0.host", "localhost");
    LOG_INFO << "Database port: "
             << common::config::ConfigManager::get<int>(config, "db_clients.0.port", 5432);
    LOG_INFO << "Redis host: "
             << common::config::ConfigManager::get<std::string>(
                  config, "redis_clients.0.host", "localhost"
                );

    // Setup CORS support
    setupCors();

    // Fail fast on a defective Error Catalog: validate the single source of
    // truth invariants at startup (Requirement 3.5). A violation aborts the
    // process so a defective build is never released.
    drogon::app().registerBeginningAdvice([]() {
        common::error::ErrorCatalog::validateInvariants();
        LOG_INFO << "ErrorCatalog invariants validated";
    });

    // Global Security Headers
    drogon::app().registerPostHandlingAdvice(
      [](const drogon::HttpRequestPtr &req, const drogon::HttpResponsePtr &resp) {
          resp->addHeader("X-Content-Type-Options", "nosniff");
          resp->addHeader("X-Frame-Options", "SAMEORIGIN");

          // Only apply CSP to HTML pages to avoid breaking API calls
          if (resp->getContentType() == drogon::CT_TEXT_HTML)
          {
              std::string path = req->path();
              if (path.find("/docs/") == 0)
              {
                  // Swagger UI needs relaxed CSP
                  resp->addHeader(
                    "Content-Security-Policy",
                    "default-src 'self'; "
                    "script-src 'self' 'unsafe-inline' 'unsafe-eval' https://unpkg.com; "
                    "style-src 'self' 'unsafe-inline' https://fonts.googleapis.com "
                    "https://unpkg.com; "
                    "font-src 'self' https://fonts.gstatic.com; "
                    "img-src 'self' data: https:; "
                    "connect-src 'self' https://unpkg.com; "
                    "frame-ancestors 'self';"
                  );
              }
              else
              {
                  // Strict CSP for main application
                  resp->addHeader(
                    "Content-Security-Policy",
                    "default-src 'self'; "
                    "script-src 'self'; "
                    "style-src 'self' https://fonts.googleapis.com; "
                    "font-src 'self' https://fonts.gstatic.com; "
                    "img-src 'self' data:; "
                    "connect-src 'self'; "
                    "frame-ancestors 'none'; "
                    "base-uri 'self'; "
                    "form-action 'self';"
                  );
              }
          }

          // Only set HSTS header on HTTPS connections
          // Check X-Forwarded-Proto header for reverse proxy scenarios
          auto forwardedProto = req->getHeader("X-Forwarded-Proto");
          if (forwardedProto == "https")
          {
              resp->addHeader("Strict-Transport-Security", "max-age=31536000; includeSubDomains");
          }
      }
    );

    // Report Hodor status after plugins have been initialized. Hodor is loaded
    // only by production configuration. When present, also wire its rejection
    // response factory so rate-limited requests get the standard Error Envelope
    // (VALIDATION_RATE_LIMITED / HTTP 429) instead of Hodor's plain-text body.
    // If the plugin is absent or its load state cannot be determined, startup
    // proceeds normally and rate-limit Envelope semantics are simply not
    // guaranteed (Requirements 6.4-6.7).
    drogon::app().registerBeginningAdvice([]() {
        try
        {
            auto hodor = drogon::app().getPlugin<drogon::plugin::Hodor>();
            if (hodor)
            {
                hodor->setRejectResponseFactory([](const drogon::HttpRequestPtr &req) {
                    common::error::Error error = common::error::Error::fromCode(
                      "VALIDATION_RATE_LIMITED", common::error::RequestId::resolve(req)
                    );
                    return common::error::ErrorResponder::buildResponse(req, error);
                });
                LOG_INFO << "Hodor rate limiter enabled";
            }
            else
                LOG_INFO << "Hodor rate limiter not enabled by this config";
        }
        catch (const std::exception &)
        {
            LOG_INFO << "Hodor rate limiter not enabled by this config";
        }
    });

    // Initialize API documentation
    LOG_INFO << "Initializing API documentation...";

    // Explicitly register the OAuth2 standard controller's OpenApi docs before
    // generating the spec. Previously these were registered via a file-scope
    // global object's constructor at static-init time (cross-TU SIOF, defect
    // 1.1); registration is now an explicit, order-independent startup step.
    // initApiDocs() is idempotent (call_once), so this is safe even though the
    // plugin also invokes it during initAndStart().
    oauth2::controllers::OAuth2StandardController::initApiDocs();

    // Configure OpenAPI server from config
    const auto &listeners = drogon::app().getListeners();
    const auto &customConfig = drogon::app().getCustomConfig();
    if (
      !listeners.empty() && customConfig.isMember("listeners") &&
      customConfig["listeners"].isArray() && !customConfig["listeners"].empty()
    )
    {
        const auto &listener = listeners[0];
        const auto &listenerConfig = customConfig["listeners"][0];

        std::string host = listener.toIp();
        uint16_t port = listener.toPort();
        bool isHttps = listenerConfig.get("https", false).asBool();

        // Build server URL
        std::string serverUrl;
        if (isHttps)
        {
            serverUrl = "https://" + host;
        }
        else
        {
            serverUrl = "http://" + host;
        }

        // Add port if not default
        if ((isHttps && port != 443) || (!isHttps && port != 80))
        {
            serverUrl += ":" + std::to_string(port);
        }

        oauth2::observability::openapi::OpenApiGenerator::setServerConfig(
          serverUrl, "OAuth2 Authorization Server"
        );
    }

    // Use current working directory (usually build/Release or project root)
    std::filesystem::path baseDir = std::filesystem::current_path();
    std::string openapiPath = (baseDir / "docs" / "api" / "openapi.json").string();

    if (!oauth2::observability::openapi::OpenApiGenerator::writeToFile(openapiPath))
    {
        LOG_WARN << "Failed to write OpenAPI specification";
    }
    else
    {
        LOG_INFO << "OpenAPI specification generated: " << openapiPath;
    }

    // Swagger UI is available at http://localhost:8080/docs/api/swagger-ui/
    // Static files are served from document_root configured in config.json

    // Global Exception Handler
    drogon::app().setExceptionHandler(
      [](
        const std::exception &e,
        const drogon::HttpRequestPtr &req,
        std::function<void(const drogon::HttpResponsePtr &)> &&callback
      ) {
          LOG_ERROR << "Unhandled exception: " << e.what() << " on path: " << req->path();

          // Branch by request path: OAuth2 protocol endpoints must keep emitting
          // an RFC 6749 §5.2 `server_error` body; every other Application_Endpoint
          // gets a unified Error Envelope (INTERNAL_ERROR). The existing CORS
          // header injection is preserved on both branches (Requirement 7.7).
          const std::string &path = req->path();
          const bool isOAuth2Protocol =
            path.rfind("/oauth2/", 0) == 0 || path == "/.well-known/oauth-authorization-server" ||
            path == "/.well-known/openid-configuration" || path == "/.well-known/jwks.json";

          // Wrap the callback so CORS headers are injected on whichever response
          // the chosen branch produces (mirrors the prior behavior).
          auto withCors = [req,
                           callback = std::move(callback)](const drogon::HttpResponsePtr &resp) {
              const auto &origin = req->getHeader("Origin");
              if (!origin.empty())
              {
                  resp->addHeader("Access-Control-Allow-Origin", origin);
                  resp->addHeader("Access-Control-Allow-Credentials", "true");
              }
              callback(resp);
          };

          if (isOAuth2Protocol)
          {
              // RFC 6749 §5.2 protocol error: { "error": "server_error", ... }
              // driven by the Catalog (default error_description, status 500).
              common::error::OAuth2ErrorHandler::sendErrorResponse(
                std::move(withCors), common::error::OAuth2ErrorHandler::SERVER_ERROR
              );
              return;
          }

          // Application path: unified Error Envelope with INTERNAL_ERROR.
          common::error::Error error = common::error::Error::fromCode(
            std::string(common::error::ErrorCatalog::internalError().code),
            common::error::RequestId::resolve(req)
          );
          auto resp = common::error::ErrorResponder::buildResponse(req, error);
          withCors(resp);
      }
    );

    // Run database migrations before starting the server
    {
        std::filesystem::path migrationsDir;
        // Try relative paths from likely working directories
        if (std::filesystem::exists("sql/migrations"))
            migrationsDir = "sql/migrations";
        else if (std::filesystem::exists("../sql/migrations"))
            migrationsDir = "../sql/migrations";
        else if (std::filesystem::exists("../../OAuth2Server/sql/migrations"))
            migrationsDir = "../../OAuth2Server/sql/migrations";
        else if (std::filesystem::exists("../../../OAuth2Server/sql/migrations"))
            migrationsDir = "../../../OAuth2Server/sql/migrations";

        if (!migrationsDir.empty())
        {
            LOG_INFO << "Schema migrations directory found: "
                     << std::filesystem::absolute(migrationsDir).string();

            // Auto-migration is opt-in via OAUTH2_AUTO_MIGRATE=true
            // In production, use setup_database.bat or CI pipeline for migrations
            const char *autoMigrate = std::getenv("OAUTH2_AUTO_MIGRATE");
            if (autoMigrate && std::string(autoMigrate) == "true")
            {
                std::string migrationsDirStr = migrationsDir.string();
                drogon::app().registerBeginningAdvice([migrationsDirStr]() {
                    // Run in a detached thread to avoid blocking the event loop
                    std::thread([migrationsDirStr]() {
                        // Small delay to ensure DB pool is ready
                        std::this_thread::sleep_for(std::chrono::milliseconds(500));
                        if (!schema::SchemaManager::migrate(migrationsDirStr))
                        {
                            LOG_ERROR << "Schema migration failed!";
                        }
                    }).detach();
                });
            }
            else
            {
                LOG_INFO << "Auto-migration disabled. Set OAUTH2_AUTO_MIGRATE=true to enable.";
            }
        }
        else
        {
            LOG_WARN << "Migrations directory not found, skipping schema migration";
        }
    }

    drogon::app().run();
    return 0;
}
