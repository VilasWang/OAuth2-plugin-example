---
name: db-reset
description: 重置测试数据库（清空并重新初始化所有表结构和数据）
disable-model-invocation: true
---

# 数据库重置技能

⚠️ **警告**：此操作会清空所有数据并重建数据库！

## 使用方法

通过用户调用：`/db-reset`

## 数据库凭据

| 配置项 | 值 |
|-------|-----|
| 主机 | localhost |
| 端口 | 5432 |
| 用户名 | oauth2_user |
| 密码 | 123456 |
| 数据库 | oauth2_db |

## 完整工作流程

### 1. 停止后端服务

```powershell
# Windows
Get-Process -Name "OAuth2Server" -ErrorAction SilentlyContinue | Stop-Process -Force
```

### 2. 删除并重建数据库

```powershell
$env:PGPASSWORD = "123456"
psql -h localhost -U oauth2_user -d postgres -c "DROP DATABASE IF EXISTS oauth2_db;"
psql -h localhost -U oauth2_user -d postgres -c "CREATE DATABASE oauth2_db;"
```

### 3. 执行 Migration 脚本

数据库 schema 统一通过 `OAuth2Server/sql/migrations/` 目录管理（V001-V018）。

```powershell
$env:PGPASSWORD = "123456"
Get-ChildItem "OAuth2Server\sql\migrations\V*.sql" | Sort-Object Name | ForEach-Object {
    psql -h localhost -U oauth2_user -d oauth2_db -f $_.FullName
    if ($LASTEXITCODE -eq 0) { Write-Host "✅ $($_.Name)" }
    else { Write-Host "❌ $($_.Name)"; exit 1 }
}
```

### 4. 执行 Seed 数据

```powershell
Get-ChildItem "OAuth2Server\sql\seed\*.sql" | ForEach-Object {
    psql -h localhost -U oauth2_user -d oauth2_db -f $_.FullName
    if ($LASTEXITCODE -eq 0) { Write-Host "✅ $($_.Name)" }
    else { Write-Host "⚠️  $($_.Name) (non-critical)" }
}
$env:PGPASSWORD = $null
Write-Host "`n🎉 Database reset completed!"
```

### 5. 验证

```powershell
$env:PGPASSWORD = "123456"
psql -h localhost -U oauth2_user -d oauth2_db -c "\dt"
$env:PGPASSWORD = $null
```

## SQL 目录结构

```
OAuth2Server/sql/
├── migrations/          # Schema 定义（按版本顺序执行）
│   ├── V001__schema_migrations.sql
│   ├── V002__oauth2_core.sql
│   ├── V003__oauth2_core_indexes.sql
│   ├── V004__users_table.sql
│   ├── V005__rbac_schema.sql
│   ├── V006__oauth2_scopes.sql
│   ├── V007__user_public_sub.sql
│   ├── ...
│   └── V018__webauthn.sql
└── seed/                # 开发/测试环境初始数据
    ├── dev_admin_user.sql
    ├── dev_admin_console_client.sql
    ├── dev_backend_client.sql
    └── dev_vue_client.sql
```

> **注意**: 旧的 `sql/001_*.sql` ~ `sql/004_*.sql` 已废弃删除，所有 schema 统一在 `migrations/` 管理。

## 故障排除

### 数据库正在被使用
```sql
SELECT pg_terminate_backend(pid) FROM pg_stat_activity
WHERE datname = 'oauth2_db' AND pid <> pg_backend_pid();
```

### 权限不足
```sql
GRANT ALL PRIVILEGES ON DATABASE oauth2_db TO oauth2_user;
ALTER DATABASE oauth2_db OWNER TO oauth2_user;
```

## 相关技能

- `/orm-gen` - 重置后重新生成 ORM 模型
- `/build-and-test` - 重建后运行测试
