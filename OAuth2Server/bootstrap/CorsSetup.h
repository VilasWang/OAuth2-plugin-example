#pragma once

// M3 Task 25 (authforge-sdk-refactor, design.md §6 "apps/server ...
// src/bootstrap/ # CorsSetup/SecurityHeaders/ExceptionHandler/
// OpenApiSetup/MigrationRunner"): extracted from main.cc's setupCors().
// Registers Drogon sync/post-handling advice for CORS preflight (OPTIONS)
// requests and response headers, driven by the `cors.allow_origins`
// custom_config array (strict exact-match whitelist, no wildcards).

namespace bootstrap
{

// Registers the CORS sync advice (preflight OPTIONS handling) and
// post-handling advice (response header injection) on drogon::app().
// Must be called before drogon::app().run().
void setupCors();

}  // namespace bootstrap
