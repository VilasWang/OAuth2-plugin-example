---
description: 重置测试数据库（清空并重新初始化）
---

# 数据库重置

> ⚠️ **警告**：此操作会清空所有数据！

## 1. 停止后端服务

// turbo

```powershell
taskkill /F /IM OAuth2Server.exe 2>$null
```

## 2. 清空并重建数据库

```powershell
# 设置密码环境变量避免交互式输入
$env:PGPASSWORD='123456'

# 删除并重建数据库
psql -U test -d postgres -c "DROP DATABASE IF EXISTS oauth_test;"
psql -U test -d postgres -c "CREATE DATABASE oauth_test;"
```

## 3. 执行 SQL 初始化脚本

// turbo

```powershell
cd d:\work\development\Repos\backend\drogon-plugin\OAuth2-plugin-example
$env:PGPASSWORD='123456'
psql -U test -d oauth_test -f "OAuth2Backend\sql\001_oauth2_core.sql"
psql -U test -d oauth_test -f "OAuth2Backend\sql\002_users_table.sql"
psql -U test -d oauth_test -f "OAuth2Backend\sql\003_rbac_schema.sql"
Write-Host "✅ 数据库已重置"
```

## 数据库凭据

| 配置项 | 值 |
|-------|-----|
| 用户名 | test |
| 密码 | 123456 |
| 数据库 | oauth_test |
| 端口 | 5432 |

## SQL 文件列表

| 文件 | 用途 |
|------|------|
| `001_oauth2_core.sql` | OAuth2 核心表（clients, codes, tokens） |
| `002_users_table.sql` | 用户账号表 |
| `003_rbac_schema.sql` | RBAC 权限架构及默认数据 |
