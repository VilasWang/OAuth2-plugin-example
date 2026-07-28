-- DEV ONLY: CONFIDENTIAL client for testing client_credentials grant
-- Secret: 'test-secret', Salt: 'test-salt'
-- DO NOT use in production!

INSERT INTO oauth2_clients (client_id, client_type, client_secret, salt, name, redirect_uris, allowed_grant_types)
VALUES (
    'backend-svc',
    'CONFIDENTIAL',
    'ec9b3755fdb189372fd52f952f3fb2f9568d50490fc04d8af4bb6bb35c4c915f',
    'test-salt',
    'Backend Service (Test)',
    '',
    'client_credentials'
)
ON CONFLICT (client_id) DO NOTHING;
