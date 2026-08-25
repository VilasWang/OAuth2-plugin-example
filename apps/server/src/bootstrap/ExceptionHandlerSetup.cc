#include "ExceptionHandlerSetup.h"
#include <drogon/drogon.h>
#include <fulla/common/error/ErrorCatalog.h>
#include <fulla/drogon/error/ErrorResponder.h>
#include <fulla/common/error/ErrorTypes.h>
#include <fulla/drogon/error/OAuth2ErrorHandler.h>
#include <fulla/drogon/error/RequestId.h>
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
              fulla::common::error::OAuth2ErrorHandler::sendErrorResponse(
                std::move(withCors), fulla::common::error::OAuth2ErrorHandler::SERVER_ERROR
              );
              return;
          }

          // Application path: unified Error Envelope with INTERNAL_ERROR.
          fulla::common::error::Error error = fulla::common::error::Error::fromCode(
            std::string(fulla::common::error::ErrorCatalog::internalError().code),
            fulla::common::error::RequestId::resolve(req)
          );
          auto resp = fulla::common::error::ErrorResponder::buildResponse(req, error);
          withCors(resp);
      }
    );
}

}  // namespace bootstrap
