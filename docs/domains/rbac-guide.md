# RBAC Access Control System (Role-Based Access Control)

This document details the design and usage of the system's role-based access control (RBAC).

## 1. Core Concepts

The system adopts the standard RBAC model:

- **User**: the acting subject of the system.
- **Role**: a collection of permissions (e.g., `admin`, `user`).
- **Permission**: a specific access capability (e.g., `user:delete`, `sys:monitor`) - *Note: currently simplified to role-based URL interception*.

### Relationship Model

- `User <-> Role`: many-to-many
- `Role <-> Permission`: many-to-many

## 2. Database Design

Relevant table structures (PostgreSQL):

```sql
-- Users table
CREATE TABLE users (...);

-- Roles table
CREATE TABLE roles (
    id SERIAL PRIMARY KEY,
    name VARCHAR(50) UNIQUE NOT NULL, -- e.g. 'admin', 'user'
    description TEXT
);

-- User-role association table
CREATE TABLE user_roles (
    user_id INT REFERENCES users(id),
    role_id INT REFERENCES roles(id),
    PRIMARY KEY (user_id, role_id)
);
```

## 3. Configuration Rules (rbac_rules)

Configure the mapping between URL paths and required roles in `config.json`:

```json
"rbac_rules": {
    "/api/admin/.*": ["admin"],       // admin only
    "/api/user/.*": ["user", "admin"] // user or admin
}
```

- **Logic**: OR logic (possessing any single role in the list is sufficient to pass).
- **Matching**: URL paths are matched by regular expression.

## 4. Authentication Flow

1. **Login/Registration**:
   - On registration, users are automatically assigned the default `user` role.
   - On login, the system queries the `user_roles` table to load all of the user's roles.
2. **Token Issuance**:
   - The `roles` list is included in the token response (JSON body).
   - `roles` are also issued into the JWT claims along with the access token (written by TokenService at issuance).
3. **Request Interception (AuthorizationFilter)**:
   - Parse the access token to obtain the `userId`.
   - Query the cache/database by `userId` to fetch the current roles.
   - Match the request URL against `rbac_rules`.
   - Verify that the user holds a required role.
   - **Pass**: continue processing.
   - **Deny**: return `403 Forbidden`.

## 5. Management Endpoints

- **Dashboard**: `/api/admin/dashboard` (requires the `admin` role)

## 6. How to Grant Admin Privileges

Currently this must be granted manually via SQL (in production it is usually done through the SuperAdmin UI):

```sql
-- Assume the target user ID is 5 and the Admin role ID is 1
INSERT INTO user_roles (user_id, role_id) VALUES (5, 1);
```
