#include "OrganizationService.h"
#include <authforge/drogon/adapters/DrogonAuditSink.h>
#include <oauth2/plugin/OAuth2Plugin.h>

#include <authforge/storage/postgres/models/Organizations.h>
#include <authforge/drogon/error/ErrorResponder.h>

#include <drogon/drogon.h>

#include <regex>

namespace organization
{

namespace
{
void respondError(
  const ::drogon::HttpRequestPtr &req,
  const OrganizationService::ResponseCallback &cb,
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

::drogon::orm::DbClientPtr getDbOrRespond(
  const ::drogon::HttpRequestPtr &req,
  const OrganizationService::ResponseCallback &cb
)
{
    try
    {
        return ::drogon::app().getDbClient();
    }
    catch (...)
    {
        respondError(req, cb, "DB_CONNECTION_ERROR", "Database unavailable");
        return nullptr;
    }
}

Json::Value orgRowToJson(const ::drogon_model::oauth2_db::Organizations &row)
{
    Json::Value org;
    org["id"] = row.getValueOfId();
    org["slug"] = row.getValueOfSlug();
    org["name"] = row.getValueOfName();
    org["logo_uri"] = row.getValueOfLogoUri();
    org["primary_color"] = row.getValueOfPrimaryColor();
    org["issuer_override"] = row.getValueOfIssuerOverride();
    return org;
}
}  // namespace

using namespace ::drogon::orm;
using namespace ::drogon_model::oauth2_db;

void OrganizationService::list(const ::drogon::HttpRequestPtr &req, ResponseCallback cb)
{
    auto db = getDbOrRespond(req, cb);
    if (!db)
    {
        return;
    }
    // No `created_at` is emitted in the original response (it selected the
    // column but never serialized it); orgRowToJson preserves that exactly.
    Mapper<Organizations> mapper(db);
    mapper.findBy(
      Criteria(),
      [cb](const std::vector<Organizations> &rows) {
          Json::Value json;
          Json::Value orgs(Json::arrayValue);
          for (const auto &row : rows)
          {
              orgs.append(orgRowToJson(row));
          }
          json["organizations"] = orgs;
          json["total"] = static_cast<int>(rows.size());
          (*cb)(::drogon::HttpResponse::newHttpJsonResponse(json));
      },
      [req, cb](const ::drogon::orm::DrogonDbException &e) {
          respondError(
            req, cb, "DB_QUERY_ERROR", std::string("list organizations failed: ") + e.base().what()
          );
      }
    );
}

void OrganizationService::create(const ::drogon::HttpRequestPtr &req, ResponseCallback cb)
{
    auto jsonBody = req->getJsonObject();
    if (!jsonBody)
    {
        respondError(req, cb, "VALIDATION_INVALID_INPUT", "create org: JSON body required");
        return;
    }

    std::string slug = (*jsonBody).get("slug", "").asString();
    std::string name = (*jsonBody).get("name", "").asString();
    std::string logoUri = (*jsonBody).get("logo_uri", "").asString();
    std::string primaryColor = (*jsonBody).get("primary_color", "").asString();
    std::string issuerOverride = (*jsonBody).get("issuer_override", "").asString();

    std::regex slugPattern("^[a-z0-9][a-z0-9-]{1,48}[a-z0-9]$");
    if (!std::regex_match(slug, slugPattern))
    {
        respondError(
          req,
          cb,
          "VALIDATION_FORMAT_ERROR",
          "create org: slug must be 3-50 chars, lowercase alphanumeric + hyphens"
        );
        return;
    }

    if (name.empty())
    {
        respondError(req, cb, "VALIDATION_MISSING_REQUIRED_FIELD", "create org: name is required");
        return;
    }

    auto db = getDbOrRespond(req, cb);
    if (!db)
    {
        return;
    }

    Organizations row;
    row.setSlug(slug);
    row.setName(name);
    row.setLogoUri(logoUri);
    row.setPrimaryColor(primaryColor);
    row.setIssuerOverride(issuerOverride);

    Mapper<Organizations> mapper(db);
    mapper.insert(
      row,
      [cb, slug, name, req](const Organizations &inserted) {
          ::authforge::drogon::adapters::DrogonAuditSink::logFromRequest(
            ::drogon::app().getPlugin<::OAuth2Plugin>()->getAuditSink(),
            "organization_created",
            "success",
            req,
            "",
            "organization",
            slug
          );
          Json::Value json;
          json["id"] = inserted.getValueOfId();
          json["slug"] = slug;
          json["name"] = name;
          json["message"] = "Organization created";
          auto resp = ::drogon::HttpResponse::newHttpJsonResponse(json);
          resp->setStatusCode(::drogon::k201Created);
          (*cb)(resp);
      },
      [req, cb](const ::drogon::orm::DrogonDbException &e) {
          // Original mapped any DB error (incl. unique-violation on slug) to
          // RESOURCE_CONFLICT; preserved.
          respondError(
            req,
            cb,
            "VALIDATION_RESOURCE_CONFLICT",
            std::string("create org: slug already exists or DB error: ") + e.base().what()
          );
      }
    );
}

void OrganizationService::getBySlug(
  const ::drogon::HttpRequestPtr &req,
  ResponseCallback cb,
  const std::string &slug
)
{
    auto db = getDbOrRespond(req, cb);
    if (!db)
    {
        return;
    }

    Mapper<Organizations> mapper(db);
    mapper.findOne(
      Criteria(Organizations::Cols::_slug, CompareOperator::EQ, slug),
      [cb](const Organizations &row) {
          Json::Value json = orgRowToJson(row);
          (*cb)(::drogon::HttpResponse::newHttpJsonResponse(json));
      },
      [req, cb](const ::drogon::orm::DrogonDbException &e) {
          // NoRowsException -> not found (original empty-result branch).
          respondError(req, cb, "VALIDATION_RESOURCE_NOT_FOUND", "get org: organization not found");
          (void)e;
      }
    );
}

}  // namespace organization
