# RBAC 权限控制系统 (Role-Based Access Control)

本文档详细说明了系统的基于角色的访问控制 (RBAC) 设计与使用。

## 1. 核心概念

系统采用标准的 RBAC 模型：

- **User (用户)**: 系统的操作主体。
- **Role (角色)**: 权限的集合 (e.g., `admin`, `user`)。
- **Permission (权限)**: 具体的访问能力 (e.g., `user:delete`, `sys:monitor`) - *注: 目前简化为基于角色的 URL 拦截*。

### 关系模型

- User <-> Role: 多对多 (Many-to-Many)
- Role <-> Permission: 多对多 (Many-to-Many)

## 2. 数据库设计

相关表结构 (PostgreSQL):

```sql
-- 用户表
CREATE TABLE users (...);

-- 角色表
CREATE TABLE roles (
    id SERIAL PRIMARY KEY,
    name VARCHAR(50) UNIQUE NOT NULL, -- e.g. 'admin', 'user'
    description TEXT
);

-- 用户-角色关联表
CREATE TABLE user_roles (
    user_id INT REFERENCES users(id),
    role_id INT REFERENCES roles(id),
    PRIMARY KEY (user_id, role_id)
);
```

## 3. 配置规则 (rbac_rules)

在 `config.json` 中配置 URL 路径与所需角色的映射：

```json
"rbac_rules": {
    "/api/admin/.*": ["admin"],       // 仅 admin 可访问
    "/api/user/.*": ["user", "admin"] // user 或 admin 均可访问
}
```

- **逻辑**: OR 逻辑 (只要具备列表中任意一个角色即可通过)。
- **匹配**: 正则表达式匹配 URL Path。

## 4. 认证流程

1. **登录/注册**:
   - 用户注册时，默认自动分配 `user` 角色。
   - 用户登录时，系统查询 `user_roles` 表，获取用户所有角色。
2. **Token 颁发**:
   - `roles` 列表被包含在 Token 响应中 (JSON body)。
   - (未来支持) `roles` 可签发进 JWT Claim。
3. **请求拦截 (AuthorizationFilter)**:
   - 解析 Access Token 获取 `userId`。
   - 根据 `userId` 查询缓存/数据库获取当前角色。
   - 匹配请求 URL 是否命中 `rbac_rules`。
   - 验证用户是否持有要求角色。
   - **通过**: 继续处理。
   - **拒绝**: 返回 `403 Forbidden`。

## 5. 管理接口

- **Dashboard**: `/api/admin/dashboard` (需 `admin` 角色)

## 6. 如何授予 Admin 权限

目前需通过 SQL 手动授予（生产环境通常由 SuperAdmin 界面操作）：

```sql
-- 假设目标用户 ID 为 5，Admin 角色 ID 为 1
INSERT INTO user_roles (user_id, role_id) VALUES (5, 1);
```
