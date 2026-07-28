-- V017: Multi-tenancy support (Organizations)
CREATE TABLE IF NOT EXISTS organizations (
    id SERIAL PRIMARY KEY,
    slug VARCHAR(50) UNIQUE NOT NULL,
    name VARCHAR(200) NOT NULL,
    logo_uri VARCHAR(512),
    primary_color VARCHAR(7),
    issuer_override VARCHAR(512),
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

-- Add org_id to users (nullable for backward compatibility)
ALTER TABLE users ADD COLUMN IF NOT EXISTS org_id INTEGER REFERENCES organizations(id);

-- Add org_id to oauth2_clients (nullable for backward compatibility)
ALTER TABLE oauth2_clients ADD COLUMN IF NOT EXISTS org_id INTEGER REFERENCES organizations(id);

-- Indexes
CREATE INDEX IF NOT EXISTS idx_users_org ON users(org_id);
CREATE INDEX IF NOT EXISTS idx_clients_org ON oauth2_clients(org_id);
CREATE INDEX IF NOT EXISTS idx_organizations_slug ON organizations(slug);
