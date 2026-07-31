#pragma once

// Task B5 (authforge-sdk-refactor): application-service extraction for the
// dynamic client registration (RFC 7591) non-admin domain. Replaces raw SQL
// INSERT INTO oauth2_clients in ClientRegistrationController with
// Mapper<Oauth2Clients>::insert (per db-operations.md).
//
// Lives in libs/drogon (Adapter layer, namespace
// authforge::drogon::services).

#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>

#include <functional>
#include <memory>
#include <string>

namespace authforge::drogon::services
{

class ClientRegistrationService
{
  public:
    using ResponseCallback =
      std::shared_ptr<std::function<void(const ::drogon::HttpResponsePtr &)>>;

    // ---- POST /api/oauth2/register (RFC 7591) ----
    static void registerClient(const ::drogon::HttpRequestPtr &req, ResponseCallback cb);
};

}  // namespace authforge::drogon::services
