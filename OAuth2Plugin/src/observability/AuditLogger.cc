#include <oauth2/observability/AuditLogger.h>
#include <drogon/drogon.h>
#include <oauth2/adapters/DrogonLogger.h>
#include <oauth2/plugin/OAuth2Plugin.h>
#include <authforge/storage/postgres/models/AuditLogs.h>

namespace authforge::drogon::observability
{

namespace
{
authforge::common::ports::ILogger &logger()
{
    static authforge::drogon::adapters::DrogonLogger instance;
    return instance;
}
}  // namespace

void AuditLogger::log(const authforge::common::observability::AuditEvent &event)
{
    // Skip if storage type is memory
    auto plugin = ::drogon::app().getPlugin<OAuth2Plugin>();
    if (plugin && plugin->getStorageType() == "memory")
    {
        return;
    }
    // Async write to database via ORM Mapper - fire and forget (don't block main flow)
    try
    {
        auto db = ::drogon::app().getDbClient();
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

        drogon_model::oauth2_db::AuditLogs auditLog;
        auditLog.setActorType(event.actorType);
        auditLog.setActorId(event.actorId);
        auditLog.setAction(event.action);
        auditLog.setTargetType(event.targetType);
        auditLog.setTargetId(event.targetId);
        auditLog.setOutcome(event.outcome);
        auditLog.setIp(event.ip);
        auditLog.setUserAgent(event.userAgent);
        auditLog.setRequestId(event.requestId);
        auditLog.setDetails(detailsStr);

        auto sharedCb =
          std::make_shared<std::function<void(const ::drogon::orm::DrogonDbException &)>>(
            [action = event.action](const ::drogon::orm::DrogonDbException &e) {
                logger().log(
                  authforge::common::ports::LogLevel::Warn,
                  "AuditLogger: Mapper insert FAILED: " + std::string(e.base().what()) +
                    " (action=" + action + ")"
                );
            }
          );

        LOG_DEBUG << "[AuditLogger] Starting Mapper::insert for action=" << event.action;

        ::drogon::orm::Mapper<drogon_model::oauth2_db::AuditLogs> mapper(db);
        mapper.insert(
          auditLog,
          [action = event.action](const drogon_model::oauth2_db::AuditLogs &) {
              LOG_DEBUG << "[AuditLogger] Mapper::insert OK for action=" << action;
          },
          [sharedCb](const ::drogon::orm::DrogonDbException &e) { (*sharedCb)(e); }
        );
    }
    catch (const std::exception &e)
    {
        logger().log(
          authforge::common::ports::LogLevel::Warn,
          "AuditLogger: Exception: " + std::string(e.what())
        );
    }
}

}  // namespace authforge::drogon::observability
