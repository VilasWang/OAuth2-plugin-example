#pragma once

// M3 Task 20 continuation (authforge-sdk-refactor): relocated from
// OAuth2Plugin/include/oauth2/controllers/OAuth2StandardController.h into
// libs/drogon, using the AutoCreation=false + explicit registerController
// pattern verified across the rest of Task 20's controller migration (see
// PROGRESS.md). Namespace is authforge::drogon::controllers (finalized in
// M8 Task 40's namespace-unification pass; the call sites in
// OAuth2Server/main.cc, OAuth2Plugin.cc, OAuth2FlowE2ETest.cc, and
// CategoryA_InitOrderSnapshotTest.cc were updated to match). design.md's
// naming cleanup (§5.8, splitting into AuthorizationEndpointController/
// TokenEndpointController/DiscoveryController) is deferred to a later task.

#include <drogon/HttpController.h>
#include <oauth2/plugin/OAuth2Plugin.h>

namespace authforge::drogon::controllers
{

// Helper struct to hold client credentials and authentication scheme
struct ClientCredentials
{
    std::string clientId;
    std::string clientSecret;
    std::string authScheme;  // "Basic" if from Authorization header, empty otherwise
};

// Lifetime / concurrency contract (defect 1.11, design Property 3):
//
// Drogon controllers are process-wide singletons whose lifetime spans the entire
// run of the process. `this` therefore remains valid for the duration of every
// async callback issued from a controller method; the implicit framework guarantee
// is made explicit here as a contract.
//
// Consequences enforced throughout this controller:
//   - The controller MUST hold NO mutable shared state. Async callbacks never
//     capture a raw `this` to reach controller members; they capture only the
//     locals they need plus the plugin pointer.
//   - Dependencies whose lifetime is NOT covered by the singleton guarantee (most
//     importantly the OAuth2 storage) are captured as a std::shared_ptr and threaded
//     along the async chain (NOT held as a raw IOAuth2Storage*), so they are kept
//     alive across every async hop. See OAuth2Plugin::getStorage(), which returns
//     std::shared_ptr<oauth2::IOAuth2Storage> for exactly this reason.
class OAuth2StandardController : public ::drogon::HttpController<OAuth2StandardController, false>
{
  public:
    static void initApiDocs();

    // M3 Task 23 (authforge-sdk-refactor, evaluation H4): see
    // libs/drogon/include/authforge/drogon/controllers/HealthController.h's
    // setPlugin() comment for the full rationale.
    void setPlugin(::OAuth2Plugin *plugin)
    {
        plugin_ = plugin;
    }

    METHOD_LIST_BEGIN
    ADD_METHOD_TO(OAuth2StandardController::authorize, "/oauth2/authorize", ::drogon::Get);
    ADD_METHOD_TO(OAuth2StandardController::token, "/oauth2/token", ::drogon::Post);
    ADD_METHOD_TO(
      OAuth2StandardController::userInfo,
      "/oauth2/userinfo",
      ::drogon::Get,
      "authforge::drogon::filters::OAuth2AuthFilter"
    );
    ADD_METHOD_TO(
      OAuth2StandardController::introspect,
      "/oauth2/introspect",
      ::drogon::Post,
      "authforge::drogon::filters::OAuth2AuthFilter"
    );
    ADD_METHOD_TO(
      OAuth2StandardController::revoke,
      "/oauth2/revoke",
      ::drogon::Post,
      "authforge::drogon::filters::OAuth2AuthFilter"
    );
    ADD_METHOD_TO(
      OAuth2StandardController::metadata,
      "/.well-known/oauth-authorization-server",
      ::drogon::Get
    );
    ADD_METHOD_TO(
      OAuth2StandardController::oidcDiscovery,
      "/.well-known/openid-configuration",
      ::drogon::Get
    );
    ADD_METHOD_TO(OAuth2StandardController::jwks, "/.well-known/jwks.json", ::drogon::Get);
    METHOD_LIST_END

    void authorize(
      const ::drogon::HttpRequestPtr &req,
      std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
    );
    void token(
      const ::drogon::HttpRequestPtr &req,
      std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
    );
    void userInfo(
      const ::drogon::HttpRequestPtr &req,
      std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
    );
    void introspect(
      const ::drogon::HttpRequestPtr &req,
      std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
    );
    void revoke(
      const ::drogon::HttpRequestPtr &req,
      std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
    );
    void metadata(
      const ::drogon::HttpRequestPtr &req,
      std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
    );
    void oidcDiscovery(
      const ::drogon::HttpRequestPtr &req,
      std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
    );
    void jwks(
      const ::drogon::HttpRequestPtr &req,
      std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
    );

  private:
    static void initApiDocsImpl();

    static ::drogon::HttpResponsePtr createSuccessResponse();

    static ClientCredentials extractClientCredentials(const ::drogon::HttpRequestPtr &req);

    static void checkUserConsentAndProceed(
      ::OAuth2Plugin *plugin,
      const std::string &clientId,
      const std::string &userId,
      int32_t internalUserId,
      const std::vector<std::string> &requestedScopes,
      const std::string &scope,
      const std::string &redirectUri,
      const std::string &state,
      std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
    );

    ::OAuth2Plugin *plugin_ = nullptr;

    /// Returns the injected plugin_ if set (Task 23 wiring), otherwise
    /// falls back to the global ::drogon::app().getPlugin<OAuth2Plugin>()
    /// lookup (pre-Task-23 behavior).
    ::OAuth2Plugin *resolvePlugin() const;
};

}  // namespace authforge::drogon::controllers
