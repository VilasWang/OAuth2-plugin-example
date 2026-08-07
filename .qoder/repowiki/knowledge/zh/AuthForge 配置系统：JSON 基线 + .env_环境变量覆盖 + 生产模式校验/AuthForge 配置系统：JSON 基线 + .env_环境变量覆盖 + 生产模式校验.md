---
kind: configuration_system
name: AuthForge 配置系统：JSON 基线 + .env/环境变量覆盖 + 生产模式校验
category: configuration_system
scope:
    - '**'
source_files:
    - libs/common/src/config/ConfigManager.cc
    - libs/common/include/authforge/common/config/ConfigManager.h
    - libs/common/include/authforge/common/config/ConfigTypes.h
    - apps/server/src/main.cc
    - apps/server/config/config.json
    - apps/server/config/config.dev.json
    - apps/server/config/config.prod.json
    - apps/server/config/config.ci.json
    - deploy/env/server.env.example
    - deploy/env/docker.env.example
    - deploy/helm/authforge/values.yaml
---

## 1. 总体方案

AuthForge 的后端采用 **Drogon JSON 配置文件 + 声明式环境变量覆盖** 的双层配置模型，由 `libs/common/src/config/ConfigManager.cc` 统一加载、合并与校验，再交给 Drogon 框架启动。

- **基线配置**：`apps/server/config/config.json`（默认）、`config.dev.json`、`config.prod.json`、`config.ci.json`，按运行环境选择。每个文件都是完整的 Drogon 配置文档，包含 `listeners`、`db_clients`、`redis_clients`、`app`、`plugins`、`custom_config` 等顶层段。
- **运行时覆盖**：通过 `OAUTH2_*` 系列环境变量（以及 `.env` 文件）覆盖 JSON 中的具体路径，无需复制完整配置文件。
- **启动时校验**：`ConfigManager::validate` 在进程启动阶段强制检查必填项、端口范围及生产模式安全策略。

## 2. 核心文件与职责

| 文件 | 职责 |
|---|---|
| `libs/common/include/authforge/common/config/ConfigTypes.h` | 定义 `EnvOverride` 结构体与全局 `OAUTH2_ENV_OVERRIDES` 规则表，声明哪些环境变量映射到 JSON 的哪个路径 |
| `libs/common/include/authforge/common/config/ConfigManager.h` | 暴露 `load` / `validate` / `get<T>` / `applyEnvOverrides` / `getEnv` 静态接口 |
| `libs/common/src/config/ConfigManager.cc` | 实现 JSON 解析、`.env` 文件扫描、环境变量覆盖、JSON Pointer 导航（含数组名过滤 `[name=OAuth2Plugin]`）、类型转换与生产模式校验 |
| `apps/server/src/main.cc` | 定位并加载 `config.json`，调用 `ConfigManager::load` 与 `validate`，将结果传给 `drogon::app().loadConfigJson` |
| `apps/server/config/{config.json,config.dev.json,config.prod.json,config.ci.json}` | 各环境的基线配置（Drogon + OAuth2Plugin + custom_config） |
| `deploy/env/server.env.example` / `docker.env.example` | 环境变量清单与注释，说明优先级与必填项 |
| `deploy/helm/authforge/values.yaml` | Helm Chart 值模板，将敏感值放入 Kubernetes Secret，非敏感值注入为 Pod 环境变量 |

## 3. 架构与约定

### 3.1 加载顺序与优先级

1. 进程启动后 `main.cc` 从当前目录向上查找 `config.json`（`./config.json` → `../config.json` → `../../../config.json`），找不到则直接退出。
2. `ConfigManager::load` 用 JsonCpp 解析该 JSON 到 `Json::Value`。
3. 首次访问时懒加载 `.env` 文件（搜索 `.env`、`../.env`、`../../.env`，支持 `KEY=VALUE`、`#` 注释、去除首尾空白与引号包裹的值）。
4. 调用 `applyEnvOverrides(config, OAUTH2_ENV_OVERRIDES)`，对每条规则执行：**.env 文件 > 系统环境变量 > JSON 中已有值**。
5. 调用 `validate(config, errorMessage)`，失败则 `LOG_FATAL` 并 `exit(1)`。
6. 最终把合并后的 `Json::Value` 传给 `drogon::app().loadConfigJson`。

### 3.2 环境变量覆盖机制

`OAUTH2_ENV_OVERRIDES` 是 `std::vector<EnvOverride>`，每条规则指定：
- `configPath`：JSON 路径，如 `db_clients.0.host`、`custom_config.metadata.issuer`、`plugins[name=OAuth2Plugin].config.clients.vue-client.redirect_uri`。
- `envVar`：环境变量名，如 `OAUTH2_DB_HOST`、`OAUTH2_ISSUER`、`OAUTH2_VUE_REDIRECT_URI`。
- `isNumeric`：是否按整数解析。
- `isStringList`：是否按逗号分隔拆分为 JSON 字符串数组（用于 `OAUTH2_CORS_ALLOW_ORIGINS`）。

JSON Pointer 导航支持三种语法：
- 对象成员：`a.b.c`
- 数组索引：`arr.0.field`
- 数组元素按字段过滤：`plugins[name=OAuth2Plugin].config...`，解耦插件在数组中的位置差异。

### 3.3 配置结构约定

所有环境配置文件共享同一份 schema：
- `listeners[]`：Drogon HTTP/HTTPS 监听器。
- `db_clients[]`：PostgreSQL 连接池（host/port/dbname/user/passwd/number_of_connections/timeout/auto_batch/connect_options）。`db_clients.0.passwd` 必须通过 `OAUTH2_DB_PASSWORD` 覆盖，禁止使用 `123456` / `password`。
- `redis_clients[]`：Redis 客户端（host/port/passwd/number_of_connections/timeout）。
- `app.*`：Drogon 应用级参数（线程数、session、静态资源、日志、压缩、keepalive 等）。
- `plugins[]`：Drogon 插件列表，其中 `OAuth2Plugin.config` 描述存储后端、Redis/Postgres 引用、预注册 client、admin_users、token TTL、清理间隔等。
- `custom_config.*`：业务配置区，包括 `rbac_rules`、`cors.allow_origins`、`frontend.url/register_path`、`external_auth.{github,wechat,google}`、`metadata.issuer`、`auth.require_email_verification` 等。

### 3.4 生产模式校验（ConfigManager::validate）

当 `OAUTH2_ENV=production` 时，启动阶段强制：
- `custom_config.metadata.issuer`（或 `OAUTH2_ISSUER`）必须存在且以 `https://` 开头；否则拒绝启动。
- `db_clients.0.passwd` 不能为空、不能是 `123456` 或 `password`。
- `redis_clients.0.passwd` 不能是 `123456` 或 `password`。

这些规则由 `ConfigManager.cc` 硬编码实现，任何绕过都会导致进程立即退出。

### 3.5 部署侧配置传递

- **本地开发**：复制 `deploy/env/server.env.example` 为 `.env`，放在可执行文件同级或父目录，`ConfigManager` 自动发现。
- **Docker Compose**：`deploy/docker/docker-compose.prod.yml` 通过 `--env-file .env.docker` 传入 `OAUTH2_*` 变量；前端构建期通过 `VITE_*` 构建参数内联到 SPA 包中（构建时而非运行时）。
- **Kubernetes/Helm**：`deploy/helm/authforge/values.yaml` 将敏感值（DB/Redis/Client Secret/SMTP/GitHub/Google 密码）放入 `secrets.*`，渲染为 Kubernetes Secret；非敏感值（issuer、listenPort、frontendUrl、corsAllowOrigins 等）作为 Pod 环境变量注入。Chart 注释明确“敏感值仅通过 Secret 传递，不会写入文件或日志”。

## 4. 约束与规则

- **唯一配置入口**：所有运行时配置必须经 `ConfigManager::load` + `validate` 流程，`main.cc` 在失败时直接 `exit(1)`，不存在旁路读取。
- **环境变量命名规范**：所有覆盖键以 `OAUTH2_` 前缀（除 `DETAILED_VALIDATION_ERRORS` 和 `OAUTH2_ENV_OVERRIDES` 自身），新增覆盖必须在 `OAUTH2_ENV_OVERRIDES` 中声明对应规则，否则无法被覆盖。
- **禁止明文密码**：生产模式下 DB/Redis 密码不允许使用默认值；JWT 私钥通过 `OAUTH2_JWT_KEY_PATH`（挂载只读卷 `/app/keys/signing.pem`）或 `OAUTH2_SIGNING_KEY` 提供，未设置时使用临时密钥（重启即失效，仅限开发）。
- **前端 URL 一致性**：`OAUTH2_FRONTEND_URL` 同时影响 CORS 白名单与 OIDC issuer 生成；Helm 要求 `issuer` 与 `frontendUrl` 指向同一域名。
- **迁移开关**：`OAUTH2_AUTO_MIGRATE=true` 才会在启动时执行迁移；Helm 默认关闭，改由独立的 Job Hook 负责。
- **日志安全**：启动日志仅打印 DB/Redis host 与 port，不打印密码；Helm values 注释强调“nothing here is ever written to files or logs by the server”。

## 5. 相关测试

`tests/unit/config/` 下包含针对 `ConfigManager` 的单元测试（例如覆盖 `.env` 加载、JSON Pointer 导航、数组名过滤、类型转换、生产模式校验等），确保配置加载逻辑变更时有回归保障。

## 6. 适用性判断

本仓库存在完整、集中且多环境一致的配置系统，涵盖 JSON 基线、环境变量覆盖、`.env` 文件、生产模式校验、Helm/Docker 集成，属于高置信度的配置系统实现。