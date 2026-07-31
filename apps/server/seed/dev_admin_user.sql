-- DEV ONLY: Default admin user for development/testing
-- Password: 'admin', Salt: 'admin_salt'
-- DO NOT use in production!

INSERT INTO users (username, password_hash, salt, email)
VALUES ('admin', '892738161086b314334f88d661aa6e7bab7c825c34bf55222811dad46cdbf724', 'admin_salt', 'admin@example.com')
ON CONFLICT (username) DO NOTHING;

-- Assign admin role to admin user
INSERT INTO user_roles (user_id, role_id)
SELECT u.id, r.id
FROM users u, roles r
WHERE u.username = 'admin' AND r.name = 'admin'
ON CONFLICT DO NOTHING;

-- Create subject mapping for admin
INSERT INTO oauth2_subject_mappings (subject, internal_user_id, provider)
SELECT u.id::text, u.id, 'local'
FROM users u WHERE u.username = 'admin'
ON CONFLICT (provider, subject) DO NOTHING;
