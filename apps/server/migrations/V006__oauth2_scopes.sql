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

-- OAuth2 scope catalog (#43 resource-scope authorization model).
--
-- The legacy bare 'read'/'write' rows are DROPPED (decision #2: no deployed
-- tokens to break). Replaced by resource-prefixed <resource>:<action> scopes
-- plus the OIDC standard set and the 'admin' super-scope. Every admin-family
-- scope has requires_admin_role = TRUE so Tier-2 (admin-role check) is driven
-- by this column, not a hardcoded list (§5.5).
INSERT INTO oauth2_scopes (name, description, mapped_role, is_default, requires_admin_role) VALUES
    -- OIDC standard scopes (RFC 6749 §3.3 / OIDC Core §5.4) -- unchanged.
    ('openid', 'OpenID Connect identity verification', 'user', TRUE, FALSE),
    ('profile', 'Access user basic profile (username)', 'user', TRUE, FALSE),
    ('email', 'Access user email address', 'user', FALSE, FALSE),
    -- Super-scope: 'admin' implies every <resource>:<action> below at the
    -- resource layer (per-requirement impliedBy, NOT a DB graph). It still
    -- requires the admin role at Tier-2.
    ('admin', 'Administrator privileges (implies all admin resource scopes)', 'admin', FALSE, TRUE),
    -- User management (#43: splits the former blanket 'admin' on /api/admin/users*).
    ('users:read', 'Read user accounts and roles', 'admin', FALSE, TRUE),
    ('users:write', 'Create, update, enable, disable users and assign roles', 'admin', FALSE, TRUE),
    -- Client (application) management.
    ('clients:read', 'Read OAuth2 client applications and their scopes', 'admin', FALSE, TRUE),
    ('clients:write', 'Create, update, delete clients and manage client scopes/secrets', 'admin', FALSE, TRUE),
    -- Token management + revocation.
    ('tokens:read', 'List active tokens', 'admin', FALSE, TRUE),
    ('tokens:write', 'Revoke tokens by client, user, or prefix', 'admin', FALSE, TRUE),
    -- Roles + scopes catalog management.
    ('roles:read', 'Read roles and the scope catalog', 'admin', FALSE, TRUE),
    ('roles:write', 'Create, update, delete roles and scopes', 'admin', FALSE, TRUE),
    -- Audit logs + dashboard + OIDC key info (read-only admin viewing).
    ('audit:read', 'View audit logs, dashboard statistics, and OIDC key info', 'admin', FALSE, TRUE)
ON CONFLICT (name) DO NOTHING;
