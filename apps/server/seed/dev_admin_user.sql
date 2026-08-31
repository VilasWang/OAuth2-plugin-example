-- DEV ONLY: Default admin user for development/testing
-- Password: 'admin', Salt: 'admin_salt'
-- DO NOT use in production!

INSERT INTO users (username, password_hash, salt, email)
VALUES ('admin', '$pbkdf2-sha256$310000$61646d696e5f736565645f73616c74$6c0307305e1390e1214b15f1f4d0250b2de86aa0e8aa0e008e5cca03084d3d62', '', 'admin@example.com')
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
