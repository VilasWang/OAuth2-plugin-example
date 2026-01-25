-- RBAC Schema Migration
-- Phase 28: Role-Based Access Control

-- 1. Roles Table
CREATE TABLE IF NOT EXISTS roles (
    id SERIAL PRIMARY KEY,
    name VARCHAR(50) NOT NULL UNIQUE,
    description TEXT,
    created_at TIMESTAMP WITH TIME ZONE DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP WITH TIME ZONE DEFAULT CURRENT_TIMESTAMP
);

-- 2. Permissions Table
CREATE TABLE IF NOT EXISTS permissions (
    id SERIAL PRIMARY KEY,
    name VARCHAR(100) NOT NULL UNIQUE,
    description TEXT,
    created_at TIMESTAMP WITH TIME ZONE DEFAULT CURRENT_TIMESTAMP
);

-- 3. User-Role Junction Table (Composite PK)
CREATE TABLE IF NOT EXISTS user_roles (
    user_id INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    role_id INTEGER NOT NULL REFERENCES roles(id) ON DELETE CASCADE,
    assigned_at TIMESTAMP WITH TIME ZONE DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (user_id, role_id)
);

-- 4. Role-Permission Junction Table (Composite PK)
CREATE TABLE IF NOT EXISTS role_permissions (
    role_id INTEGER NOT NULL REFERENCES roles(id) ON DELETE CASCADE,
    permission_id INTEGER NOT NULL REFERENCES permissions(id) ON DELETE CASCADE,
    PRIMARY KEY (role_id, permission_id)
);

-- Indexes for faster lookups
CREATE INDEX IF NOT EXISTS idx_user_roles_user ON user_roles(user_id);
CREATE INDEX IF NOT EXISTS idx_user_roles_role ON user_roles(role_id);
CREATE INDEX IF NOT EXISTS idx_role_permissions_role ON role_permissions(role_id);

-- Default Roles
INSERT INTO roles (name, description) VALUES 
    ('admin', 'System Administrator with full access'),
    ('user', 'Standard user with basic access')
ON CONFLICT (name) DO NOTHING;

-- Default Permissions (Example)
INSERT INTO permissions (name, description) VALUES
    ('user:read', 'View user profiles'),
    ('user:write', 'Edit user profiles'),
    ('user:delete', 'Delete users'),
    ('admin:access', 'Access admin panel')
ON CONFLICT (name) DO NOTHING;

-- Assign all permissions to admin role
INSERT INTO role_permissions (role_id, permission_id)
SELECT r.id, p.id FROM roles r, permissions p WHERE r.name = 'admin'
ON CONFLICT DO NOTHING;

-- Assign basic permissions to user role
INSERT INTO role_permissions (role_id, permission_id)
SELECT r.id, p.id FROM roles r, permissions p 
WHERE r.name = 'user' AND p.name IN ('user:read')
ON CONFLICT DO NOTHING;

-- 6. Default Admin User (Bootstrap)
-- Password: 'admin', Salt: 'admin_salt'
INSERT INTO users (username, password_hash, salt, email)
VALUES ('admin', '892738161086b314334f88d661aa6e7bab7c825c34bf55222811dad46cdbf724', 'admin_salt', 'admin@example.com')
ON CONFLICT (username) DO NOTHING;

-- Assign admin role to admin user
INSERT INTO user_roles (user_id, role_id)
SELECT u.id, r.id
FROM users u, roles r
WHERE u.username = 'admin' AND r.name = 'admin'
ON CONFLICT DO NOTHING;

