#pragma once

#include <string>
#include <vector>
#include <json/json.h>

namespace common::config
{

// Environment variable override configuration
struct EnvOverride
{
    std::string configPath;  // JSON path like "db_clients.0.host"
    const char *envVar;      // Environment variable name
    bool isNumeric;          // Is numeric type
    bool isStringList = false;  // Comma-separated string → JSON array of strings
};

// OAuth2 environment variable override rules
inline const std::vector<EnvOverride> OAUTH2_ENV_OVERRIDES =
  {{"db_clients.0.host", "OAUTH2_DB_HOST", false},
   {"db_clients.0.port", "OAUTH2_DB_PORT", true},
   {"db_clients.0.dbname", "OAUTH2_DB_NAME", false},
   {"db_clients.0.user", "OAUTH2_DB_USER", false},
   {"db_clients.0.passwd", "OAUTH2_DB_PASSWORD", false},
   {"redis_clients.0.host", "OAUTH2_REDIS_HOST", false},
   {"redis_clients.0.port", "OAUTH2_REDIS_PORT", true},
   {"redis_clients.0.passwd", "OAUTH2_REDIS_PASSWORD", false},
   {"custom_config.metadata.issuer", "OAUTH2_ISSUER", false},
   {"custom_config.frontend.url", "OAUTH2_FRONTEND_URL", false},
   {"custom_config.external_auth.github.client_id", "OAUTH2_GITHUB_CLIENT_ID", false},
   {"custom_config.external_auth.github.client_secret", "OAUTH2_GITHUB_CLIENT_SECRET", false},
   {"custom_config.external_auth.google.client_id", "OAUTH2_GOOGLE_CLIENT_ID", false},
   {"custom_config.external_auth.google.client_secret", "OAUTH2_GOOGLE_CLIENT_SECRET", false},
   {"custom_config.external_auth.google.redirect_uri", "OAUTH2_GOOGLE_REDIRECT_URI", false},
   {"listeners.0.port", "OAUTH2_LISTEN_PORT", true},
   {"vue_client.secret", "OAUTH2_VUE_CLIENT_SECRET", false},
   // Index 2 matches config.prod.json (PromExporter=0, Hodor=1, OAuth2Plugin=2).
   // This is the file baked into the Docker runtime image (Dockerfile copies
   // config.prod.json → config.json). Production deployments must keep OAuth2Plugin
   // at this index, or update the path accordingly.
   {"plugins.2.config.clients.vue-client.redirect_uri", "OAUTH2_VUE_REDIRECT_URI", false},
   {"custom_config.cors.allow_origins", "OAUTH2_CORS_ALLOW_ORIGINS", false, /*isStringList=*/true}};

}  // namespace common::config
