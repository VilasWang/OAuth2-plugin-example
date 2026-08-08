-- V002: OAuth2 Core Tables (Clients, Codes, Tokens)
-- Idempotent: uses IF NOT EXISTS

-- Clients Table
CREATE TABLE IF NOT EXISTS oauth2_clients (
    client_id VARCHAR(50) PRIMARY KEY,
    client_type VARCHAR(20) NOT NULL DEFAULT 'CONFIDENTIAL',
    client_secret VARCHAR(100) NOT NULL,
    salt VARCHAR(50) NOT NULL,
    name VARCHAR(100),
    redirect_uris TEXT,
    allowed_grant_types TEXT,
    -- F-017 (RFC 7591 §2 / RFC 6749 §3.2.1): per-client token-endpoint auth
    -- method. NULL preserves the legacy lenient behavior (Basic -> body
    -- fallback); explicit values are enforced at token/introspect/revoke.
    token_endpoint_auth_method VARCHAR(50)
);

-- Authorization Codes Table
CREATE TABLE IF NOT EXISTS oauth2_codes (
    code VARCHAR(100) PRIMARY KEY,
    client_id VARCHAR(50) NOT NULL REFERENCES oauth2_clients(client_id),
    user_id VARCHAR(50),
    scope TEXT,
    redirect_uri TEXT,
    code_challenge VARCHAR(128),
    code_challenge_method VARCHAR(10),
    expires_at BIGINT NOT NULL,
    used BOOLEAN DEFAULT FALSE,
    -- F-021/F-022 (OIDC Core §2/§3.1.3.7): carried from the login session to
    -- the id_token at code exchange. auth_time = epoch of the user's most
    -- recent authentication; amr = space-separated authentication-method
    -- refs ("pwd", "mfa", ...).
    auth_time BIGINT,
    amr VARCHAR(128)
);

-- Access Tokens Table
CREATE TABLE IF NOT EXISTS oauth2_access_tokens (
    token VARCHAR(100) PRIMARY KEY,
    client_id VARCHAR(50) NOT NULL REFERENCES oauth2_clients(client_id),
    user_id VARCHAR(50),
    scope TEXT,
    expires_at BIGINT NOT NULL,
    revoked BOOLEAN DEFAULT FALSE,
    issued_at BIGINT NOT NULL DEFAULT EXTRACT(EPOCH FROM CURRENT_TIMESTAMP)::BIGINT,
    issuer VARCHAR(255) NOT NULL DEFAULT '',
    audience VARCHAR(255),
    not_before BIGINT DEFAULT EXTRACT(EPOCH FROM CURRENT_TIMESTAMP)::BIGINT,
    introspect_count INTEGER DEFAULT 0,
    revoked_at BIGINT,
    revoked_by VARCHAR(50)
);

-- Refresh Tokens Table
CREATE TABLE IF NOT EXISTS oauth2_refresh_tokens (
    token VARCHAR(100) PRIMARY KEY,
    access_token VARCHAR(100) NOT NULL,
    client_id VARCHAR(50) NOT NULL REFERENCES oauth2_clients(client_id),
    user_id VARCHAR(50),
    scope TEXT,
    expires_at BIGINT NOT NULL,
    revoked BOOLEAN DEFAULT FALSE,
    revoked_at BIGINT,
    revoked_by VARCHAR(50)
);
