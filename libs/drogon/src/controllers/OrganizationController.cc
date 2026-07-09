#include <authforge/drogon/controllers/OrganizationController.h>
#include <oauth2/observability/AuditLogger.h>
#include <oauth2/observability/openapi/OpenApiGenerator.h>
#include <oauth2/error/ErrorResponder.h>
#include <drogon/drogon.h>
#include <regex>

namespace authforge::drogon::controllers
{

namespace
{

// Emit an Application error via the unified ErrorResponder entry point so the
// body is always an Error Envelope (Requirement 7.1 / 7.3 / 7.5).
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

struct OrganizationControllerDocs
{
    OrganizationControllerDocs()
    {
        oauth2::observability::openapi::EndpointInfo getOrgsDocs;
        getOrgsDocs.path = "/api/orgs";
        getOrgsDocs.method = "GET";
        getOrgsDocs.summary = "List Organizations";
        getOrgsDocs.description = "List all organizations.";
        getOrgsDocs.tags = {"Organization"};
        getOrgsDocs.requiresAuth = true;
        oauth2::observability::openapi::OpenApiGenerator::addEndpoint(getOrgsDocs);

        oauth2::observability::openapi::EndpointInfo postOrgsDocs;
        postOrgsDocs.path = "/api/orgs";
        postOrgsDocs.method = "POST";
        postOrgsDocs.summary = "Create Organization";
        postOrgsDocs.description = "Create a new organization.";
        postOrgsDocs.tags = {"Organization"};
        postOrgsDocs.requiresAuth = true;
        oauth2::observability::openapi::OpenApiGenerator::addEndpoint(postOrgsDocs);

        oauth2::observability::openapi::EndpointInfo postOrgUsersDocs;
        postOrgUsersDocs.path = "/api/orgs/{orgId}/users";
        postOrgUsersDocs.method = "POST";
        postOrgUsersDocs.summary = "Add User to Organization";
        postOrgUsersDocs.description = "Add a user to an organization.";
        postOrgUsersDocs.tags = {"Organization"};
        postOrgUsersDocs.requiresAuth = true;
        oauth2::observability::openapi::OpenApiGenerator::addEndpoint(postOrgUsersDocs);
    }
};

OrganizationControllerDocs docs_;

}  // namespace

void OrganizationController::list(
  const ::drogon::HttpRequestPtr &req,
  std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
)
{
    auto sharedCb =
      std::make_shared<std::function<void(const ::drogon::HttpResponsePtr &)>>(std::move(callback));
    auto db = ::drogon::app().getDbClient();
    db->execSqlAsync(
      "SELECT id, slug, name, logo_uri, primary_color, issuer_override, created_at "
      "FROM organizations ORDER BY id",
      [sharedCb](const ::drogon::orm::Result &r) {
          Json::Value json;
          Json::Value orgs(Json::arrayValue);
          for (const auto &row : r)
          {
              Json::Value org;
              org["id"] = row["id"].as<int>();
              org["slug"] = row["slug"].as<std::string>();
              org["name"] = row["name"].as<std::string>();
              org["logo_uri"] = row["logo_uri"].isNull() ? "" : row["logo_uri"].as<std::string>();
              org["primary_color"] =
                row["primary_color"].isNull() ? "" : row["primary_color"].as<std::string>();
              org["issuer_override"] =
                row["issuer_override"].isNull() ? "" : row["issuer_override"].as<std::string>();
              orgs.append(org);
          }
          json["organizations"] = orgs;
          json["total"] = static_cast<int>(r.size());
          (*sharedCb)(::drogon::HttpResponse::newHttpJsonResponse(json));
      },
      [sharedCb, req](const ::drogon::orm::DrogonDbException &e) {
          respondError(
            req,
            sharedCb,
            "DB_QUERY_ERROR",
            std::string("list organizations failed: ") + e.base().what()
          );
      }
    );
}

void OrganizationController::create(
  const ::drogon::HttpRequestPtr &req,
  std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
)
{
    auto sharedCb =
      std::make_shared<std::function<void(const ::drogon::HttpResponsePtr &)>>(std::move(callback));
    auto jsonBody = req->getJsonObject();
    if (!jsonBody)
    {
        respondError(req, sharedCb, "VALIDATION_INVALID_INPUT", "create org: JSON body required");
        return;
    }

    std::string slug = (*jsonBody).get("slug", "").asString();
    std::string name = (*jsonBody).get("name", "").asString();
    std::string logoUri = (*jsonBody).get("logo_uri", "").asString();
    std::string primaryColor = (*jsonBody).get("primary_color", "").asString();
    std::string issuerOverride = (*jsonBody).get("issuer_override", "").asString();

    // Validate slug: lowercase alphanumeric + hyphens, 3-50 chars
    std::regex slugPattern("^[a-z0-9][a-z0-9-]{1,48}[a-z0-9]$");
    if (!std::regex_match(slug, slugPattern))
    {
        respondError(
          req,
          sharedCb,
          "VALIDATION_FORMAT_ERROR",
          "create org: slug must be 3-50 chars, lowercase alphanumeric + hyphens"
        );
        return;
    }

    if (name.empty())
    {
        respondError(
          req, sharedCb, "VALIDATION_MISSING_REQUIRED_FIELD", "create org: name is required"
        );
        return;
    }

    auto db = ::drogon::app().getDbClient();
    db->execSqlAsync(
      "INSERT INTO organizations (slug, name, logo_uri, primary_color, issuer_override) "
      "VALUES ($1, $2, $3, $4, $5) RETURNING id",
      [sharedCb, slug, name, req](const ::drogon::orm::Result &r) {
          int id = r[0]["id"].as<int>();
          oauth2::observability::AuditLogger::log(
            "organization_created", "success", req, "", "organization", slug
          );
          Json::Value json;
          json["id"] = id;
          json["slug"] = slug;
          json["name"] = name;
          json["message"] = "Organization created";
          auto resp = ::drogon::HttpResponse::newHttpJsonResponse(json);
          resp->setStatusCode(::drogon::k201Created);
          (*sharedCb)(resp);
      },
      [sharedCb, req](const ::drogon::orm::DrogonDbException &e) {
          respondError(
            req,
            sharedCb,
            "VALIDATION_RESOURCE_CONFLICT",
            std::string("create org: slug already exists or DB error: ") + e.base().what()
          );
      },
      slug,
      name,
      logoUri,
      primaryColor,
      issuerOverride
    );
}

void OrganizationController::getBySlug(
  const ::drogon::HttpRequestPtr &req,
  std::function<void(const ::drogon::HttpResponsePtr &)> &&callback,
  const std::string &slug
)
{
    auto sharedCb =
      std::make_shared<std::function<void(const ::drogon::HttpResponsePtr &)>>(std::move(callback));
    auto db = ::drogon::app().getDbClient();
    db->execSqlAsync(
      "SELECT id, slug, name, logo_uri, primary_color, issuer_override, created_at "
      "FROM organizations WHERE slug = $1",
      [sharedCb, req](const ::drogon::orm::Result &r) {
          if (r.empty())
          {
              respondError(
                req, sharedCb, "VALIDATION_RESOURCE_NOT_FOUND", "get org: organization not found"
              );
              return;
          }
          auto row = r[0];
          Json::Value json;
          json["id"] = row["id"].as<int>();
          json["slug"] = row["slug"].as<std::string>();
          json["name"] = row["name"].as<std::string>();
          json["logo_uri"] = row["logo_uri"].isNull() ? "" : row["logo_uri"].as<std::string>();
          json["primary_color"] =
            row["primary_color"].isNull() ? "" : row["primary_color"].as<std::string>();
          json["issuer_override"] =
            row["issuer_override"].isNull() ? "" : row["issuer_override"].as<std::string>();
          (*sharedCb)(::drogon::HttpResponse::newHttpJsonResponse(json));
      },
      [sharedCb, req](const ::drogon::orm::DrogonDbException &e) {
          respondError(
            req, sharedCb, "DB_QUERY_ERROR", std::string("get org failed: ") + e.base().what()
          );
      },
      slug
    );
}

}  // namespace authforge::drogon::controllers
