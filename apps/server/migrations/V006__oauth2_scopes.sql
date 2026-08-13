-- V006: OAuth2 Scopes, Client-Scopes, User Consents, Subject Mappings

CREATE TABLE IF NOT EXISTS oauth2_scopes (
    id SERIAL PRIMARY KEY,
    name VARCHAR(100) UNIQUE NOT NULL,
    description TEXT,
    mapped_role VARCHAR(50),
    is_default BOOLEAN DEFAULT FALSE,
    requires_admin_role BOOLEAN DEFAULT FALSE
);

CREATE TABLE IF NOT EXISTS oauth2_client_scopes (
    id SERIAL PRIMARY KEY,
    client_id VARCHAR(50) REFERENCES oauth2_clients(client_id) ON DELETE CASCADE,
    scope_name VARCHAR(100) REFERENCES oauth2_scopes(name) ON DELETE CASCADE,
    UNIQUE(client_id, scope_name)
);

CREATE TABLE IF NOT EXISTS oauth2_user_consents (
    id SERIAL PRIMARY KEY,
    internal_user_id INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    client_id VARCHAR(50) REFERENCES oauth2_clients(client_id) ON DELETE CASCADE,
    scope_name VARCHAR(100) REFERENCES oauth2_scopes(name) ON DELETE CASCADE,
    granted_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    UNIQUE(internal_user_id, client_id, scope_name)
);

CREATE TABLE IF NOT EXISTS oauth2_subject_mappings (
    id SERIAL PRIMARY KEY,
    subject VARCHAR(128) NOT NULL,
    internal_user_id INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    provider VARCHAR(100) DEFAULT 'local',
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    UNIQUE(provider, subject)
);

-- Indexes
CREATE INDEX IF NOT EXISTS idx_oauth2_client_scopes_lookup ON oauth2_client_scopes(client_id);
CREATE INDEX IF NOT EXISTS idx_oauth2_user_consents_lookup ON oauth2_user_consents(internal_user_id, client_id);
CREATE INDEX IF NOT EXISTS idx_oauth2_user_consents_user ON oauth2_user_consents(internal_user_id);
CREATE INDEX IF NOT EXISTS idx_oauth2_user_consents_client ON oauth2_user_consents(client_id);
CREATE INDEX IF NOT EXISTS idx_oauth2_subject_mappings_provider_subject ON oauth2_subject_mappings(provider, subject);
CREATE INDEX IF NOT EXISTS idx_oauth2_subject_mappings_user ON oauth2_subject_mappings(internal_user_id);

-- Default OAuth2 scopes (idempotent)
INSERT INTO oauth2_scopes (name, description, mapped_role, is_default, requires_admin_role) VALUES
    ('openid', 'OpenID Connect identity verification', 'user', TRUE, FALSE),
    ('profile', 'Access user basic info (username, email)', 'user', TRUE, FALSE),
    ('email', 'Access user email address', 'user', FALSE, FALSE),
    ('admin', 'Administrator privileges', 'admin', FALSE, TRUE),
    ('read', 'Read-only access', 'user', FALSE, FALSE),
    ('write', 'Write access', 'user', FALSE, FALSE)
ON CONFLICT (name) DO NOTHING;
