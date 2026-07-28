#pragma once

// M3 Task 25 (authforge-sdk-refactor): extracted from main.cc's inline
// setExceptionHandler lambda. Branches uncaught exceptions by path:
// OAuth2 protocol endpoints keep emitting an RFC 6749 §5.2 server_error
// body; every other Application_Endpoint gets a unified Error Envelope
// (INTERNAL_ERROR). CORS headers are preserved on both branches.

namespace bootstrap
{

// Registers the global uncaught-exception handler on drogon::app(). Must
// be called before drogon::app().run().
void setupExceptionHandler();

}  // namespace bootstrap
