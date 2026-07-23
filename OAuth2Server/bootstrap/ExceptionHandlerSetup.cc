#include "ExceptionHandlerSetup.h"
#include <drogon/drogon.h>
#include <oauth2/error/ErrorCatalog.h>
#include <oauth2/error/ErrorResponder.h>
#include <oauth2/error/ErrorTypes.h>
#include <oauth2/error/OAuth2ErrorHandler.h>
#include <oauth2/error/RequestId.h>
#include <string>

namespace bootstrap
{

void setupExceptionHandler()
{
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
              authforge::common::error::OAuth2ErrorHandler::sendErrorResponse(
                std::move(withCors), authforge::common::error::OAuth2ErrorHandler::SERVER_ERROR
              );
              return;
          }

          // Application path: unified Error Envelope with INTERNAL_ERROR.
          authforge::common::error::Error error = authforge::common::error::Error::fromCode(
            std::string(authforge::common::error::ErrorCatalog::internalError().code),
            authforge::common::error::RequestId::resolve(req)
          );
          auto resp = authforge::common::error::ErrorResponder::buildResponse(req, error);
          withCors(resp);
      }
    );
}

}  // namespace bootstrap
