-- DEV ONLY: CONFIDENTIAL client for testing client_credentials grant
-- Secret: 'test-secret', Salt: 'test-salt'
-- DO NOT use in production!

INSERT INTO oauth2_clients (client_id, client_type, client_secret, salt, name, redirect_uris, allowed_grant_types, token_endpoint_auth_method)
VALUES (
    'backend-svc',
    'CONFIDENTIAL',
    'ec9b3755fdb189372fd52f952f3fb2f9568d50490fc04d8af4bb6bb35c4c915f',
    'test-salt',
    'Backend Service (Test)',
    '',
    'client_credentials',
    'client_secret_basic'
)
ON CONFLICT (client_id) DO NOTHING;

-- Grant scopes to backend-svc (#43: the legacy 'read'/'write' scopes are
-- dropped; backend-svc now gets the resource-scope vocabulary. The
-- client_credentials scope-validation test exercises these.)
INSERT INTO oauth2_client_scopes (client_id, scope_name)
SELECT 'backend-svc', name FROM oauth2_scopes
WHERE name IN ('tokens:read', 'tokens:write', 'clients:read', 'users:read')
ON CONFLICT (client_id, scope_name) DO NOTHING;
