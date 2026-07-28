-- DEV ONLY: Sample OAuth2 client for Vue frontend development
-- DO NOT use in production!

INSERT INTO oauth2_clients (client_id, client_type, client_secret, salt, name, redirect_uris, allowed_grant_types)
VALUES (
    'vue-client',
    'PUBLIC',
    '42a121b66fb9f1d4f73125788f42eb6799110c6aeae5a9a12a2fed5307a0088d',
    'random_salt',
    'Vue Front-end Client',
    'http://localhost:5173/callback,http://localhost:8080/callback',
    'authorization_code,refresh_token'
)
ON CONFLICT (client_id) DO NOTHING;

-- Grant default scopes to vue-client
INSERT INTO oauth2_client_scopes (client_id, scope_name)
SELECT 'vue-client', name
FROM oauth2_scopes
WHERE is_default = TRUE
ON CONFLICT (client_id, scope_name) DO NOTHING;
