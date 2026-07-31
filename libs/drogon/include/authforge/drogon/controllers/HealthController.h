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
// the consumer BEFORE app().run() -- see apps/server's bootstrap
// (apps/server/main.cc and tests/test_main.cc).

#include <drogon/HttpController.h>

// M3 Task 23 (authforge-sdk-refactor, evaluation H4 "controller/filter 去
// 单例化"): forward-declared so this header does not need to pull in the
// full <authforge/drogon/plugin/OAuth2Plugin.h> just to hold a pointer member.
class OAuth2Plugin;

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

    // M3 Task 23: explicit dependency injection point, set once at startup
    // (bootstrap::wireControllerPluginDependencies(), before app().run())
    // instead of every handler calling drogon::app().getPlugin<OAuth2Plugin>()
    // itself. Non-owning: the plugin's lifetime is managed by Drogon's
    // PluginsManager, which outlives every controller singleton. Each
    // handler below falls back to the global getPlugin() lookup if this is
    // unset (e.g. a caller that has not run the wiring step yet), so this
    // is an additive optimization, not a behavior-changing requirement.
    void setPlugin(OAuth2Plugin *plugin)
    {
        plugin_ = plugin;
    }

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

  private:
    OAuth2Plugin *plugin_ = nullptr;

    /// Returns the injected plugin_ if set (Task 23 wiring), otherwise
    /// falls back to the global drogon::app().getPlugin<OAuth2Plugin>()
    /// lookup (pre-Task-23 behavior). See setPlugin()'s comment.
    OAuth2Plugin *resolvePlugin() const;
};

}  // namespace authforge::drogon::controllers
