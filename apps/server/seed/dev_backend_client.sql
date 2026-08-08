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

-- Grant scopes to backend-svc (P0 #2: client_credentials now validates the
-- requested scope against oauth2_client_scopes; without this grant the
-- endpoint-script Test 10 request for scope=read would be rejected)
INSERT INTO oauth2_client_scopes (client_id, scope_name)
SELECT 'backend-svc', name FROM oauth2_scopes WHERE name IN ('read', 'write')
ON CONFLICT (client_id, scope_name) DO NOTHING;
