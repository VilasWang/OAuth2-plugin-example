#pragma once

// M3 Task 25 (authforge-sdk-refactor): extracted from main.cc's inline
// registerPostHandlingAdvice lambda. Registers the global security
// response headers (X-Content-Type-Options, X-Frame-Options,
// Content-Security-Policy, Strict-Transport-Security).

namespace bootstrap
{

// Registers the global security headers post-handling advice on
// drogon::app(). Must be called before drogon::app().run().
void setupSecurityHeaders();

}  // namespace bootstrap
