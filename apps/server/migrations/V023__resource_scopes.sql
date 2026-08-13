-- V023: Resource-scope authorization model (#43)
--
-- Seeds the resource-prefixed <resource>:read|write scope family that the
-- ResourceScopeRegistry / filter scope gate consults. All admin-family
-- scopes have requires_admin_role = TRUE so Tier-2 (admin-role check) is
-- driven by this column, not a hardcoded list (§5.5).
--
-- The legacy bare 'read'/'write' rows from V006 are NOT deleted (the
-- migration framework forbids top-level DELETE FROM per migration_check M4).
-- They remain as inert orphan rows -- no filter or registry entry references
-- them, so they have no authorization effect. Decision #2 (drop immediately)
-- is honored at the enforcement layer (the registry), not at the data layer.
--
-- ON CONFLICT DO NOTHING: idempotent (migration_check M3).

INSERT INTO oauth2_scopes (name, description, mapped_role, is_default, requires_admin_role) VALUES
    ('users:read', 'Read user accounts and roles', 'admin', FALSE, TRUE),
    ('users:write', 'Create, update, enable, disable users and assign roles', 'admin', FALSE, TRUE),
    ('clients:read', 'Read OAuth2 client applications and their scopes', 'admin', FALSE, TRUE),
    ('clients:write', 'Create, update, delete clients and manage client scopes/secrets', 'admin', FALSE, TRUE),
    ('tokens:read', 'List active tokens', 'admin', FALSE, TRUE),
    ('tokens:write', 'Revoke tokens by client, user, or prefix', 'admin', FALSE, TRUE),
    ('roles:read', 'Read roles and the scope catalog', 'admin', FALSE, TRUE),
    ('roles:write', 'Create, update, delete roles and scopes', 'admin', FALSE, TRUE),
    ('audit:read', 'View audit logs, dashboard statistics, and OIDC key info', 'admin', FALSE, TRUE)
ON CONFLICT (name) DO NOTHING;
