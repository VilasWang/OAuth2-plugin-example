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
# 删除并重建数据库
psql -U postgres -c "DROP DATABASE IF EXISTS oauth_test;"
psql -U postgres -c "CREATE DATABASE oauth_test;"
```

## 3. 执行 SQL 初始化脚本

// turbo

```powershell
cd d:\work\development\Repos\backend\drogon-plugin\OAuth2-plugin-example\OAuth2Backend\sql
psql -U postgres -d oauth_test -f "001_oauth2_core.sql"
psql -U postgres -d oauth_test -f "002_users_table.sql"
Write-Host "✅ 数据库已重置"
```

## 4. 初始化测试客户端

```powershell
# 插入默认 OAuth2 客户端
psql -U postgres -d oauth_test -c "INSERT INTO oauth2_clients (client_id, client_secret, redirect_uri, allowed_scopes) VALUES ('vue-client', 'secret', 'http://localhost:5173/callback', 'openid profile') ON CONFLICT (client_id) DO NOTHING;"
```

## SQL 文件列表

| 文件 | 用途 |
|------|------|
| `001_oauth2_core.sql` | OAuth2 核心表（clients, codes, tokens） |
| `002_users_table.sql` | 用户账号表 |
