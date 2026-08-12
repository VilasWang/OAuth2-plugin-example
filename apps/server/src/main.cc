// M3 Task 25 (authforge-sdk-refactor, design.md §6 "apps/server/src/main.cc
// # 仅装配：读配置 → 构造实现 → 注入端口 → run"): main() has been reduced
// to pure assembly -- CorsSetup/SecurityHeaders/ExceptionHandlerSetup/
// OpenApiSetup/MigrationRunner/ControllerRegistration are now independent
// bootstrap/ modules (see bootstrap/*.h). This file only wires them
// together in the correct order and starts the server.

#include <drogon/drogon.h>
#include <drogon/plugins/Hodor.h>
#include <json/json.h>
#include <authforge/common/config/ConfigManager.h>
#include <authforge/common/error/ErrorCatalog.h>
#include <authforge/drogon/error/ErrorResponder.h>
#include <authforge/common/error/ErrorTypes.h>
#include <authforge/drogon/error/RequestId.h>
#include <authforge/drogon/controllers/AuthorizationEndpointController.h>
#include <authforge/drogon/controllers/TokenEndpointController.h>
#include <authforge/drogon/controllers/DiscoveryController.h>

#include "bootstrap/ControllerRegistration.h"
#include "bootstrap/CorsSetup.h"
#include "bootstrap/ExceptionHandlerSetup.h"
#include "bootstrap/IdentityAssembly.h"
#include "bootstrap/MigrationRunner.h"
#include "bootstrap/OpenApiSetup.h"
#include "bootstrap/SecurityHeaders.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

// Helper to parse JSON (replaces deprecated Json::Reader)
static bool parseJsonString(std::istream &stream, Json::Value &json)
{
    Json::CharReaderBuilder builder;
    std::string errs;
    return Json::parseFromStream(builder, stream, &json, &errs);
}

// Helper to create log directory from config
static void createLogDirFromConfig(const std::string &configPath)
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

// Load configuration with ConfigManager
static Json::Value loadConfiguration(const std::string &configPath)
{
    Json::Value config;

    if (!authforge::common::config::ConfigManager::load(configPath, config))
    {
        LOG_FATAL << "Failed to load configuration from: " << configPath;
        exit(1);
    }

    std::string validationError;
    if (!authforge::common::config::ConfigManager::validate(config, validationError))
    {
        LOG_FATAL << "Configuration validation failed: " << validationError;
        exit(1);
    }

    LOG_INFO << "Configuration loaded successfully";
    return config;
}

int main(int argc, char *argv[])
{
    // Task 37 (authforge-sdk-refactor): --migrate-only runs all pending
    // schema migrations synchronously and exits 0/1 without starting the
    // HTTP server. This is the entry point for the Helm
    // pre-install/pre-upgrade hook Job; regular startup is unaffected.
    bool migrateOnly = false;
    for (int i = 1; i < argc; ++i)
    {
        if (std::string(argv[i]) == "--migrate-only")
        {
            migrateOnly = true;
        }
        else
        {
            std::cerr << "Unknown argument: " << argv[i] << std::endl
                      << "Usage: authforge-server [--migrate-only]" << std::endl;
            return 1;
        }
    }

    // 1. Locate and load config.json (with env-var overrides)
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

    createLogDirFromConfig(configPath);
    Json::Value config = loadConfiguration(configPath);

    if (migrateOnly)
        return bootstrap::runMigrateOnly(config);

    drogon::app().loadConfigJson(config);

    LOG_INFO << "Database host: "
             << authforge::common::config::ConfigManager::get<std::string>(
                  config, "db_clients.0.host", "localhost"
                );
    LOG_INFO
      << "Database port: "
      << authforge::common::config::ConfigManager::get<int>(config, "db_clients.0.port", 5432);
    LOG_INFO << "Redis host: "
             << authforge::common::config::ConfigManager::get<std::string>(
                  config, "redis_clients.0.host", "localhost"
                );

    // 2. Register every AutoCreation=false controller (must precede run())
    bootstrap::registerAllControllers();

    // 3. Wire cross-cutting concerns
    bootstrap::setupCors();
    bootstrap::setupSecurityHeaders();
    bootstrap::setupExceptionHandler();

    // Fail fast on a defective Error Catalog: validate the single source of
    // truth invariants at startup (Requirement 3.5). A violation aborts the
    // process so a defective build is never released.
    drogon::app().registerBeginningAdvice([]() {
        authforge::common::error::ErrorCatalog::validateInvariants();
        LOG_INFO << "ErrorCatalog invariants validated";
    });

    // M3 Task 23 (authforge-sdk-refactor, evaluation H4): wire the
    // OAuth2Plugin pointer into every controller/filter that exposes a
    // setPlugin(), replacing their per-request
    // drogon::app().getPlugin<OAuth2Plugin>() lookups with a cached
    // pointer set once here. Must run AFTER plugins are constructed
    // (registerBeginningAdvice callbacks run once app().run() has
    // finished config-reflection plugin construction -- see
    // bootstrap::wireControllerPluginDependencies()'s header comment).
    drogon::app().registerBeginningAdvice([]() {
        bootstrap::wireControllerPluginDependencies();
        LOG_INFO << "Controller/filter plugin dependencies wired";
    });

    // Task 24 slice 4 (authforge-sdk-refactor): construct the identity-layer
    // services (authforge::identity::AuthService/SessionManager) and inject
    // them into SessionController. Must run after
    // wireControllerPluginDependencies() has registered every controller
    // (registerBeginningAdvice callbacks run in registration order) and,
    // like it, must be a registerBeginningAdvice callback itself --
    // drogon::app().getDbClient() is only reliably available once app().run()
    // has processed the db_clients config block.
    drogon::app().registerBeginningAdvice([]() { bootstrap::wireIdentityServices(); });

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
                    authforge::common::error::Error error =
                      authforge::common::error::Error::fromCode(
                        "VALIDATION_RATE_LIMITED", authforge::common::error::RequestId::resolve(req)
                      );
                    return authforge::common::error::ErrorResponder::buildResponse(req, error);
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

    // 4. OpenAPI docs (initApiDocs() is called here explicitly -- see
    // bootstrap/ControllerRegistration + OAuth2Plugin.cc's own comment on
    // why OAuth2Plugin no longer calls it itself: circular-dependency
    // avoidance between OAuth2Plugin and libs/drogon).
    LOG_INFO << "Initializing API documentation...";
    authforge::drogon::controllers::AuthorizationEndpointController::initApiDocs();
    authforge::drogon::controllers::TokenEndpointController::initApiDocs();
    authforge::drogon::controllers::DiscoveryController::initApiDocs();
    bootstrap::setupOpenApi();

    // Swagger UI is available at http://localhost:5555/docs/api
    // Static files are served from document_root configured in config.json

    // 5. Database migrations (opt-in via OAUTH2_AUTO_MIGRATE=true)
    bootstrap::setupMigrations();

    // 6. Start the server
    drogon::app().run();
    return 0;
}
