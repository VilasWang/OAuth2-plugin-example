---
description: 重置测试数据库 (oauth2_db)
---

# 数据库重置

> ⚠️ **警告**：此操作会清空 `oauth2_db` 数据库中的所有数据并重新应用 Schema！

## 1. 停止后端服务

```powershell
taskkill /F /IM OAuth2Server.exe 2>$null
```

## 2. 运行重置脚本 (Windows)

项目提供了 `scripts/backend/setup_database.bat` 脚本，它会自动删除并重建 `oauth2_db` 数据库，并按顺序应用 `OAuth2Server/sql` 目录下的所有脚本。

```powershell
.\scripts\backend\setup_database.bat
```

## 3. 运行重置脚本 (Docker)

如果你使用的是 Docker 容器内的数据库，可以使用 `scripts/backend/docker_postgres_start.bat` 来快速启动并初始化。

## 数据库凭据 (默认)

| 配置项 | 值 |
|-------|-----|
| 用户名 | oauth2_user |
| 密码 | 123456 |
| 数据库 | oauth2_db |
| 端口 | 5432 (或 5433 映射) |

## SQL 文件应用顺序

1. `001_oauth2_core.sql`: 核心授权表
2. `002_users_table.sql`: 用户表
3. `003_rbac_schema.sql`: 权限控制
4. `004_oauth2_scopes.sql`: 权限范围与授权
5. 其他表按顺序执行
