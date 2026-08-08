#pragma once

// B10 / Task 45 (design.md §5.8): split out of the former
// OAuth2StandardController (token-lifecycle portion: token/introspect/revoke/
// userInfo). AutoCreation=false.

#include <drogon/HttpController.h>
#include <authforge/drogon/plugin/OAuth2Plugin.h>

#include <string>

namespace authforge::drogon::controllers
{

// Helper struct to hold client credentials and authentication scheme
struct ClientCredentials
{
    std::string clientId;
    std::string clientSecret;
    std::string authScheme;  // "Basic" if from Authorization header, empty otherwise
};

class TokenEndpointController : public ::drogon::HttpController<TokenEndpointController, false>
{
  public:
    static void initApiDocs();

    void setPlugin(::OAuth2Plugin *plugin)
    {
        plugin_ = plugin;
    }

    METHOD_LIST_BEGIN
    ADD_METHOD_TO(TokenEndpointController::token, "/oauth2/token", ::drogon::Post);
    ADD_METHOD_TO(
      TokenEndpointController::userInfo,
      "/oauth2/userinfo",
      ::drogon::Get,
      "authforge::drogon::filters::OAuth2AuthFilter"
    );
    // RFC 7662 (introspection) and RFC 7009 (revocation) authenticate the
    // CALLING CLIENT (via HTTP Basic or form client_id/client_secret), NOT a
    // resource-owner access token. These routes therefore intentionally do NOT
    // carry OAuth2AuthFilter (which would demand a Bearer user token and
    // short-circuit with an AUTH_TOKEN_INVALID Error Envelope before the
    // handler's RFC 6749 §5.2 invalid_client path could run -- see
    // OAuth2ErrorHandler::sendErrorResponse, already RFC-compliant). The
    // handlers do their own client auth via extractClientCredentials +
    // plugin->validateClient. (Product-defect fix: previously these were
    // registered behind OAuth2AuthFilter, which both violated the RFC client-
    // credential model and masked the RFC-compliant error path the
    // OAuth2InvalidClientHeaderTest exercises.)
    ADD_METHOD_TO(
      TokenEndpointController::introspect,
      "/oauth2/introspect",
      ::drogon::Post
    );
    ADD_METHOD_TO(
      TokenEndpointController::revoke,
      "/oauth2/revoke",
      ::drogon::Post
    );
    METHOD_LIST_END

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

  private:
    static void initApiDocsImpl();

    static ::drogon::HttpResponsePtr createSuccessResponse();

    static ClientCredentials extractClientCredentials(const ::drogon::HttpRequestPtr &req);

    // F-017 (RFC 7591 §2 / RFC 6749 §3.2.1): enforce the client's declared
    // token-endpoint auth method. Returns an error description string when the
    // request's auth method conflicts with the declared one (caller emits an
    // invalid_client 401), or an empty string when the request is acceptable.
    // `declaredMethod` is the client's stored tokenEndpointAuthMethod (""
    // / unset preserves the legacy lenient Basic->body fallback). `creds`
    // is the request's extracted credentials; `secretInBody` flags whether a
    // client_secret appeared in the POST body (vs the Authorization header).
    static std::string enforceClientAuthMethod(
      const std::string &declaredMethod,
      const ClientCredentials &creds,
      bool secretInBody
    );

    ::OAuth2Plugin *plugin_ = nullptr;

    ::OAuth2Plugin *resolvePlugin() const;
};

}  // namespace authforge::drogon::controllers
