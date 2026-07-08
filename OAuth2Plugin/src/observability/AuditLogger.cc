#include <oauth2/observability/AuditLogger.h>
#include <drogon/drogon.h>
#include <oauth2/adapters/DrogonLogger.h>
#include <oauth2/adapters/OpenSslUuidGenerator.h>
#include <oauth2/plugin/OAuth2Plugin.h>

namespace oauth2::observability
{

namespace
{
// Task 14 (design.md §5.6): shared ILogger instance backing this file's
// LOG_* call sites, replacing direct Drogon LOG_* macro usage. This file
// remains Adapter-shaped overall (drogon::app().getDbClient(),
// drogon::orm::DrogonDbException, drogon::HttpRequestPtr are its actual
// job -- audit persistence -- and stay untouched; that DB/HTTP dependency
// is a separate architectural concern for the eventual M2a/M3 split of
// the audit *sink* into an Adapter package, not something Task 14's
// LOG_*/drogon::utils migration addresses). Migrating only the logging
// calls is still worthwhile: design.md §5.6 explicitly names AuditLogger
// as an ILogger call-site to migrate.
authforge::common::ports::ILogger &logger()
{
    static oauth2::adapters::DrogonLogger instance;
    return instance;
}
}  // namespace

void AuditLogger::log(const AuditEvent &event)
{
    // Skip if storage type is memory
    auto plugin = drogon::app().getPlugin<OAuth2Plugin>();
    if (plugin && plugin->getStorageType() == "memory")
    {
        return;
    }
    // Async write to database - fire and forget (don't block main flow)
    try
    {
        auto db = drogon::app().getDbClient();
        if (!db)
        {
            logger().log(
              authforge::common::ports::LogLevel::Warn,
              "AuditLogger: No DB client, logging to console only"
            );
            logger().log(
              authforge::common::ports::LogLevel::Info,
              "[AUDIT] " + event.action + " " + event.outcome + " actor=" + event.actorType + ":" +
                event.actorId + " target=" + event.targetType + ":" + event.targetId
            );
            return;
        }

        Json::StreamWriterBuilder writer;
        writer["indentation"] = "";
        std::string detailsStr =
          event.details.isNull() ? "{}" : Json::writeString(writer, event.details);

        db->execSqlAsync(
          "INSERT INTO audit_logs "
          "(actor_type, actor_id, action, target_type, target_id, outcome, "
          "ip, user_agent, request_id, details) "
          "VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10::jsonb)",
          [](const drogon::orm::Result &) {
              // Success - no action needed
          },
          [event](const drogon::orm::DrogonDbException &e) {
              logger().log(
                authforge::common::ports::LogLevel::Warn,
                "AuditLogger: Failed to write audit log: " + std::string(e.base().what()) +
                  " (action=" + event.action + ")"
              );
          },
          event.actorType,
          event.actorId,
          event.action,
          event.targetType,
          event.targetId,
          event.outcome,
          event.ip,
          event.userAgent,
          event.requestId,
          detailsStr
        );
    }
    catch (const std::exception &e)
    {
        logger().log(
          authforge::common::ports::LogLevel::Warn, "AuditLogger: Exception: " + std::string(e.what())
        );
    }
}

void AuditLogger::log(
  const std::string &action,
  const std::string &outcome,
  const drogon::HttpRequestPtr &req,
  const std::string &actorId,
  const std::string &targetType,
  const std::string &targetId,
  const Json::Value &details
)
{
    AuditEvent event;
    event.action = action;
    event.outcome = outcome;
    event.actorId = actorId;
    event.targetType = targetType;
    event.targetId = targetId;
    event.details = details;

    // Determine actor type
    if (actorId.empty())
        event.actorType = "anonymous";
    else if (actorId.find("client:") == 0)
        event.actorType = "client";
    else
        event.actorType = "user";

    // Extract request context
    if (req)
    {
        // IP: prefer X-Forwarded-For, then X-Real-IP, then peer
        event.ip = req->getHeader("X-Forwarded-For");
        if (event.ip.empty())
            event.ip = req->getHeader("X-Real-IP");
        if (event.ip.empty())
            event.ip = req->getPeerAddr().toIp();

        event.userAgent = req->getHeader("User-Agent");
        event.requestId = req->getHeader("X-Request-ID");
        if (event.requestId.empty())
        {
            // Task 14 (design.md §5.6): migrated off drogon::utils::getUuid()
            // onto the authforge::common::ports::IUuidGenerator Adapter
            // implementation (OpenSslUuidGenerator), same convention as
            // error/RequestId.cc.
            static oauth2::adapters::OpenSslUuidGenerator uuidGenerator;
            event.requestId = uuidGenerator.generate();
        }
    }

    log(event);
}

}  // namespace oauth2::observability
