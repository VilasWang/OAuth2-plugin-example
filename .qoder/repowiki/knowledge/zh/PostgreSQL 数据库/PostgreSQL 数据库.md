---
kind: external_dependency
name: PostgreSQL 数据库
slug: postgresql
category: external_dependency
category_hints:
    - client_constraint
scope:
    - '**'
source_files:
    - setup_database.bat
    - setup-database.sh
    - libs/storage-postgres/src/models/model-postgresql.json
---

### PostgreSQL
- **角色**：OAuth2 access token、用户、客户端等持久化存储；migration/seed 通过 `psql` 按序应用 SQL 文件。
- **集成点**：`OAUTH2_DB_USER/NAME/PASSWORD/HOST/PORT` 环境变量提供凭据；`setup_database.{bat,sh}` 负责建库、运行 migration、seed；`storage-postgres` 通过 drogon Mapper 访问 `oauth2_access_tokens` 表（含 `_token` hash 列与新增 `access_token` 明文列）。
- **约束**：脚本不自动创建 role，role 不存在时以「响亮失败 + 明确指引」退出；token 列表按 `issued_at DESC` 排序，`token_prefix` 为完整 token 前 8 字符。