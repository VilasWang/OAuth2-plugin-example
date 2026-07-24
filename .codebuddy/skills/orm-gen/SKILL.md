---
name: orm-gen
description: 重新生成 Drogon ORM 模型类（基于当前数据库表结构）
allowed-tools: Read, Write, Bash
---

# ORM 模型生成技能

重新生成 Drogon ORM 模型类以匹配最新的数据库表结构。

## 使用方法

通过用户调用：`/orm-gen`

## 前置条件检查

```bash
# 1. 检查 PostgreSQL 服务是否运行
pg_isready -h localhost -p 5432 || echo "PostgreSQL not running"

# 2. 检查数据库是否存在
export PGPASSWORD='123456'
psql -h localhost -U oauth2_user -d oauth2_db -c "SELECT 1;" || echo "Database oauth2_db not found"

# 3. 检查 drogon_ctl 工具是否安装
which drogon_ctl || echo "drogon_ctl not found"
drogon_ctl version || echo "drogon_ctl not working"

# 4. 检查 models 目录是否存在
ls OAuth2Server/model.json || echo "model.json not found"
```

## 完整工作流程

### 1. 确认数据库表结构

```bash
# 查看当前数据库中的所有表
export PGPASSWORD='123456'
psql -h localhost -U oauth2_user -d oauth2_db -c "\dt"

# 预期输出应包含全部19个表：
# - organizations, users, roles, permissions
# - user_roles, role_permissions
# - oauth2_clients, oauth2_codes, oauth2_access_tokens, oauth2_refresh_tokens
# - oauth2_scopes, oauth2_client_scopes, oauth2_user_consents
# - oauth2_subject_mappings, audit_logs
# - email_verification_tokens, password_reset_tokens
# - oauth2_device_codes, webauthn_credentials
```

### 2. 检查 model.json 配置

**标准配置**:
```json
{
    "rdbms": "postgresql",
    "host": "127.0.0.1",
    "port": 5432,
    "dbname": "oauth2_db",
    "user": "test",
    "passwd": "123456",
    "tables": [
        "organizations", "users", "roles", "permissions",
        "user_roles", "role_permissions",
        "oauth2_clients", "oauth2_codes", "oauth2_access_tokens", "oauth2_refresh_tokens",
        "oauth2_scopes", "oauth2_client_scopes", "oauth2_user_consents",
        "oauth2_subject_mappings", "audit_logs",
        "email_verification_tokens", "password_reset_tokens",
        "oauth2_device_codes", "webauthn_credentials"
    ]
}
```

### 3. 备份现有模型文件（推荐）

```powershell
# Windows PowerShell
cd OAuth2Server
$timestamp = Get-Date -Format "yyyyMMdd_HHmmss"
$backupDir = "models_backup_$timestamp"
New-Item -ItemType Directory -Path $backupDir | Out-Null

if (Test-Path "model.json") {
    Copy-Item model.json $backupDir\
}
if (Test-Path "models") {
    Copy-Item models\*.h, models\*.cc $backupDir\ 2>$null
}
Write-Host "Models backed up to $backupDir"
```

### 4. 执行 ORM 生成

```powershell
# Windows PowerShell - 使用专项脚本
scripts/backend/generate_models.bat -y

# 此脚本会自动完成：
# 1. 检查数据库连接
# 2. 验证 model.json 配置
# 3. 备份现有模型文件
# 4. 执行 drogon_ctl 生成
# 5. 验证生成结果
```

### 5. 手动生成 ORM 模型

```bash
# 确保在正确的目录
cd OAuth2Server/models

# 执行 drogon_ctl 生成命令
drogon_ctl create model ../
```

### 6. 验证生成结果

```bash
# 验证关键文件
ls *.h *.cc | wc -l
# 应显示 38 个文件（19 个 .h + 19 个 .cc）
```

## 生成的模型文件

| 表名 | 头文件 | 源文件 | 说明 |
|------|--------|--------|------|
| organizations | Organizations.h | Organizations.cc | 组织/租户表 |
| users | Users.h | Users.cc | 用户账号表 |
| roles | Roles.h | Roles.cc | 角色表 |
| permissions | Permissions.h | Permissions.cc | 权限表 |
| user_roles | UserRoles.h | UserRoles.cc | 用户-角色关联表 |
| role_permissions | RolePermissions.h | RolePermissions.cc | 角色-权限关联表 |
| oauth2_clients | Oauth2Clients.h | Oauth2Clients.cc | OAuth2 客户端表 |
| oauth2_codes | Oauth2Codes.h | Oauth2Codes.cc | OAuth2 授权码表 |
| oauth2_access_tokens | Oauth2AccessTokens.h | Oauth2AccessTokens.cc | OAuth2 访问令牌表 |
| oauth2_refresh_tokens | Oauth2RefreshTokens.h | Oauth2RefreshTokens.cc | OAuth2 刷新令牌表 |
| oauth2_scopes | Oauth2Scopes.h | Oauth2Scopes.cc | OAuth2 作用域表 |
| oauth2_client_scopes | Oauth2ClientScopes.h | Oauth2ClientScopes.cc | 客户端-作用域关联表 |
| oauth2_user_consents | Oauth2UserConsents.h | Oauth2UserConsents.cc | 用户授权同意表 |
| oauth2_subject_mappings | Oauth2SubjectMappings.h | Oauth2SubjectMappings.cc | OAuth2 subject-内部用户映射表 |
| audit_logs | AuditLogs.h | AuditLogs.cc | 审计日志表 |
| email_verification_tokens | EmailVerificationTokens.h | EmailVerificationTokens.cc | 邮箱验证令牌表 |
| password_reset_tokens | PasswordResetTokens.h | PasswordResetTokens.cc | 密码重置令牌表 |
| oauth2_device_codes | Oauth2DeviceCodes.h | Oauth2DeviceCodes.cc | OAuth2 设备授权码表 |
| webauthn_credentials | WebauthnCredentials.h | WebauthnCredentials.cc | WebAuthn/Passkey 凭证表 |

## 最佳实践

### 表结构变更流程
1. 修改 migration SQL 脚本
2. 重置数据库 (`/db-reset`)
3. 重新生成 ORM 模型 (`/orm-gen`)
4. 重新编译项目 (`/build-and-test`)
5. 运行测试验证

## 安全注意事项

- ORM 生成的类**禁止手动修改**
- 如需变更，应修改数据库表结构后重新生成
- model.json 包含数据库密码，注意保护
- 生成后的代码会覆盖现有文件，注意备份

## 相关技能

- `/db-reset` - 重置数据库（表结构变更前）
- `/build-and-test` - 生成后编译和测试
- `/e2e-test` - 端到端测试验证 ORM 模型
