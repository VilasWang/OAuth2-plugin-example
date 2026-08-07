---
kind: logging_system
name: 基于 Drogon LOG_* 宏 + ILogger 端口 + 结构化审计/指标日志的可观测性体系
category: logging_system
scope:
    - '**'
source_files:
    - apps/server/config/config.json
    - apps/server/config/config.dev.json
    - apps/server/config/config.prod.json
    - apps/server/config/config.ci.json
    - libs/common/include/authforge/common/ports/ILogger.h
    - libs/common/include/authforge/common/observability/AuditEvent.h
    - libs/drogon/include/authforge/drogon/observability/AuditLogger.h
    - libs/drogon/src/observability/AuditLogger.cc
    - libs/drogon/include/authforge/drogon/observability/Metrics.h
    - libs/drogon/src/observability/Metrics.cc
    - docs/backend/observability.md
---

## 1. 使用的系统与框架

- **底层日志引擎**：Drogon/Trantor 内置的 `LOG_DEBUG / LOG_INFO / LOG_WARN / LOG_ERROR / LOG_FATAL`（以及 `LOG_TRACE`）宏，通过 `apps/server/config/*.json` 中的 `app.log.use_spdlog`、`log_path`、`logfile_base_name`、`log_size_limit`、`max_files`、`log_level`、`display_local_time` 等字段控制输出目标与级别。
- **Domain 层抽象**：`libs/common/include/authforge/common/ports/ILogger.h` 定义了 `authforge::common::ports::ILogger` 接口与 `LogLevel`（Trace < Debug < Info < Warn < Error < Fatal），将 Domain 代码与 Drogon 日志宏解耦，便于单元测试用 FakeLogger 捕获断言。
- **审计日志模型**：`libs/common/include/authforge/common/observability/AuditEvent.h` 定义框架无关的 `AuditEvent`（actorType/actorId/action/targetType/targetId/outcome/ip/userAgent/requestId/details），由 Adapter 侧落库。
- **指标日志**：`libs/drogon/include/authforge/drogon/observability/Metrics.h` 提供 `Metrics` 类，当前以 `LOG_INFO << "[METRIC] ..."` 形式输出键值对，供 PromExporter/日志采集端消费；同时暴露 `OperationTimer` RAII 计时器。
- **审计写入适配器**：`libs/drogon/src/observability/AuditLogger.cc` 实现异步 DB 写入（`drogon_model::oauth2_db::AuditLogs`），在 memory 存储或无 DB 客户端时回退为控制台日志。

## 2. 关键文件与包

| 路径 | 作用 |
|---|---|
| `apps/server/config/{config,dev,prod,ci}.json` | 定义 `app.log.*` 配置项（use_spdlog、log_path、log_level 等） |
| `libs/common/include/authforge/common/ports/ILogger.h` | Domain 层日志端口（ILogger + LogLevel） |
| `libs/common/include/authforge/common/observability/AuditEvent.h` | 审计事件数据模型 |
| `libs/drogon/include/authforge/drogon/observability/AuditLogger.h` | Adapter 侧审计日志入口（fire-and-forget DB 写入） |
| `libs/drogon/src/observability/AuditLogger.cc` | AuditLogger 实现（ORM Mapper 异步插入） |
| `libs/drogon/include/authforge/drogon/observability/Metrics.h` | 指标 API（incRequest/incLoginFailure/observeLatency/updateActiveTokens 等） |
| `libs/drogon/src/observability/Metrics.cc` | 指标实现（输出 `[METRIC] key=value` 结构化行） |
| `docs/backend/observability.md` | 日志等级约定、结构化格式、调用约束的权威文档 |

## 3. 架构与约定

- **分层隔离**：Domain 层（`libs/common`、`libs/oauth2`、`libs/identity`）**不得直接使用** Drogon 的 `LOG_*` 宏，必须经 `authforge::common::ports::ILogger` 端口；Adapter/基础设施层（`libs/drogon`、`libs/storage-*`、`apps/server`）可直接使用 `LOG_*` 宏。这是设计文档与 observability.md §3.2 明确规定的约束。
- **日志级别映射**：`LogLevel::Trace→LOG_TRACE`、`Debug→LOG_DEBUG`、`Info→LOG_INFO`、`Warn→LOG_WARN`、`Error→LOG_ERROR`、`Fatal→LOG_FATAL`，与 Drogon/Trantor 严格一一对应。
- **结构化字段**：
  - 审计日志统一带 `[AUDIT]` 标记，格式为 `Action=... User=... Client=... Success=... IP=...`（见 observability.md §2.1）。
  - 指标日志统一带 `[METRIC]` 前缀并以 `key=value` 形式输出，如 `oauth2_requests_total endpoint=/authorize status=200`。
  - 上下文日志自动附带 `RequestId`，用于关联分布式追踪。
- **可观测性扩展点**：`IAuditSink` 端口让 Domain 仅依赖抽象，Adapter 侧通过 `DrogonAuditSink → AuditLogger` 完成实际持久化；`IMetrics` 端口同理，当前 Metrics 实现以结构化日志形式输出。
- **线程安全契约**：`Metrics.h` 中明确声明所有计数器必须线程安全，当前实现基于无共享状态的 `LOG_INFO` 输出，天然满足该契约。

## 4. 约定与约束

- **日志级别默认值**：生产环境 `config.prod.json` 默认 `INFO`，开发/CI 默认 `DEBUG`；可通过修改 `app.log.log_level` 动态调整（TRACE/DEBUG/INFO/WARN/ERROR/FATAL，不区分大小写）。
- **Domain 禁止直接 LOG_***：`libs/common` 内任何代码若直接调用 Drogon 的 `LOG_*` 宏即违反架构约束，必须走 `ILogger`。
- **审计事件必须走 IAuditSink**：Domain 层构造 `AuditEvent` 后通过 `IAuditSink` 提交，禁止直接调用 `AuditLogger::log`。
- **指标输出格式固定**：所有指标行必须以 `[METRIC]` 开头且采用 `metric_name label=value` 形式，以便外部采集工具解析。
- **审计写入非阻塞**：`AuditLogger::log` 是 fire-and-forget 异步 DB 写入，DB 不可用时降级为控制台日志并记录 WARN，不阻断主流程。
- **测试友好**：ILogger 的虚拟接口允许在单元测试中使用 FakeLogger 捕获并断言日志输出，无需启动 Drogon。

综上，AuthForge 的日志系统以 Drogon 内置日志为执行后端，通过 ILogger 端口将 Domain 与日志实现解耦，并以 `[AUDIT]`、`[METRIC]` 等结构化标签配合键值对字段，形成面向 Splunk/ELK/Prometheus 的可观测性体系。