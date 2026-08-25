#pragma once

// M3 Task 25 (fulla-sdk-refactor): extracted from main.cc's inline
// OpenAPI server-config + spec-generation block. Configures the
// OpenApiGenerator's server URL/description from the listener config and
// writes the generated spec to docs/api/openapi.json.

namespace bootstrap
{

// Configures the OpenAPI server URL/description from drogon::app()'s
// listeners + custom_config, then generates and writes the OpenAPI spec
// to disk. Must be called after listeners are configured (loadConfigJson)
// but before drogon::app().run().
void setupOpenApi();

}  // namespace bootstrap
