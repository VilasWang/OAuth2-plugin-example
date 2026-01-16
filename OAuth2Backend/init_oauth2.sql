-- OAuth2 Schema for PostgreSQL
-- DANGER: DROPPING EXISTING TABLES FOR CLEAN VERIFICATION

DROP TABLE IF EXISTS oauth2_refresh_tokens CASCADE;
DROP TABLE IF EXISTS oauth2_access_tokens CASCADE;
DROP TABLE IF EXISTS oauth2_codes CASCADE;
DROP TABLE IF EXISTS oauth2_clients CASCADE;

-- Clients Table
CREATE TABLE oauth2_clients (
    client_id VARCHAR(50) PRIMARY KEY,
    client_secret VARCHAR(100) NOT NULL,
    salt VARCHAR(50) NOT NULL,
    name VARCHAR(100),
    redirect_uris TEXT, -- Comma separated
    allowed_grant_types TEXT,
    allowed_scopes TEXT
);

-- Authorization Codes Table
CREATE TABLE oauth2_codes (
    code VARCHAR(100) PRIMARY KEY,
    client_id VARCHAR(50) NOT NULL REFERENCES oauth2_clients(client_id),
    user_id VARCHAR(50),
    scope TEXT,
    redirect_uri TEXT,
    expires_at BIGINT NOT NULL,
    used BOOLEAN DEFAULT FALSE
);

-- Access Tokens Table
CREATE TABLE oauth2_access_tokens (
    token VARCHAR(100) PRIMARY KEY,
    client_id VARCHAR(50) NOT NULL REFERENCES oauth2_clients(client_id),
    user_id VARCHAR(50),
    scope TEXT,
    expires_at BIGINT NOT NULL,
    revoked BOOLEAN DEFAULT FALSE
);

-- Refresh Tokens Table
CREATE TABLE oauth2_refresh_tokens (
    token VARCHAR(100) PRIMARY KEY,
    access_token VARCHAR(100) NOT NULL,
    client_id VARCHAR(50) NOT NULL REFERENCES oauth2_clients(client_id),
    user_id VARCHAR(50),
    scope TEXT,
    expires_at BIGINT NOT NULL,
    revoked BOOLEAN DEFAULT FALSE
);

-- Sample Data: vue-client (Secret: 123456)
INSERT INTO oauth2_clients (client_id, client_secret, salt, name, redirect_uris, allowed_grant_types, allowed_scopes)
VALUES (
    'vue-client', 
    '42a121b66fb9f1d4f73125788f42eb6799110c6aeae5a9a12a2fed5307a0088d', 
    'random_salt', 
    'Vue Front-end Client', 
    'http://localhost:5173/callback', 
    'authorization_code,refresh_token', 
    'openid profile'
);
