#pragma once

// M3 Task 20 slice 3 (authforge-sdk-refactor): relocated from
// OAuth2Server/controllers/HealthController.h into
// authforge::drogon::controllers, as the CORE EXPERIMENT verifying
// whether AutoCreation=false + explicit drogon::app().registerController()
// avoids the F1/H5 whole-archive requirement once a controller moves into
// a STATIC library. See PROGRESS.md's "AutoCreation=false" section for
// the full mechanism analysis (verified against Drogon's own source:
// HttpController<T, AutoCreation>'s methodRegistrator only skips the
// implicit T::initPathRouting() call when AutoCreation is false;
// DrObject<T>'s DrClassMap class-name registration is unaffected either
// way -- but with an explicit registerController call, there is a REAL,
// linker-visible reference chain to this translation unit, unlike plain
// AutoCreation=true's implicit static-initialization side effect).
//
// AutoCreation=false: this controller must be explicitly constructed and
// registered (drogon::app().registerController(make_shared<...>())) by
// the consumer BEFORE app().run() -- see apps/server's future bootstrap
// (currently OAuth2Server/main.cc and OAuth2Server/test/test_main.cc
// during this experiment).

#include <drogon/HttpController.h>

namespace authforge::drogon::controllers
{

class HealthController : public ::drogon::HttpController<HealthController, false>
{
  public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(HealthController::healthLive, "/health/live", ::drogon::Get);
    ADD_METHOD_TO(HealthController::healthReady, "/health/ready", ::drogon::Get);
    ADD_METHOD_TO(HealthController::health, "/health", ::drogon::Get);
    METHOD_LIST_END

    void health(
      const ::drogon::HttpRequestPtr &req,
      std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
    );
    void healthLive(
      const ::drogon::HttpRequestPtr &req,
      std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
    );
    void healthReady(
      const ::drogon::HttpRequestPtr &req,
      std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
    );
};

}  // namespace authforge::drogon::controllers
