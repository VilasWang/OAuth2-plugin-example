# OAuth2 可观测性设计文档 (Observability)

本系统集成了完整的 Prometheus 监控指标与结构化上下文日志，支持生产环境的实时监控与问题排查。

## 1. Prometheus Metrics

系统通过 Exporter 暴露标准的 Prometheus 指标。

### 1.1 指标列表

| 指标名称 | 类型 | 标签 (Labels) | 说明 |
|----------|------|--------------|------|
| `oauth2_requests_total` | Counter | `endpoint`, `status` | OAuth2 请求总数 |
| `oauth2_login_failures_total` | Counter | `reason` | 登录失败次数 |
| `oauth2_introspect_requests_total` | Counter | `client_id` | Token Introspection 请求总数 |
| `oauth2_introspect_errors_total` | Counter | `client_id`, `error` | Introspection 错误次数 |
| `oauth2_revocation_requests_total` | Counter | `client_id` | Token Revocation 请求总数 |
| `oauth2_revocation_errors_total` | Counter | `client_id`, `error` | Revocation 错误次数 |
| `oauth2_latency_seconds` | Histogram | `operation`, `storage` | 关键步骤（含存储后端）耗时分布 |
| `oauth2_active_tokens` | Gauge | — | 当前活跃（未过期）Token 估算值 |

> 指标由 `libs/drogon/src/observability/Metrics.cc` 与 `libs/drogon/src/adapters/DrogonMetrics.cc` 统一发射（经 `LOG_INFO` 的 `[METRIC]` 结构化日志，供 PromExporter/日志采集端消费），定义见 `libs/drogon/include/authforge/drogon/observability/Metrics.h`。

### 1.2 监控面板示例 (Grafana)

建议配置以下面板：

- **QPS & Error Rate**: `rate(oauth2_requests_total[1m])` vs `rate(oauth2_login_failures_total[1m])`
- **P99 Latency**: `histogram_quantile(0.99, rate(oauth2_latency_seconds_bucket[1m]))`
- **Business**: Active Tokens trend.

## 2. Structured Logging (结构化日志)

系统采用上下文感知的结构化日志，便于 Splunk/ELK 收集分析。

### 2.1 Audit Logs (审计日志)

关键安全操作（如颁发 Token）会输出带有 `[AUDIT]` 标记的日志。

**格式**:
`[AUDIT] Action={Action} User={UserId} Client={ClientId} Success={True/False} IP={RemoteAddr}`

**示例**:

```
2026-01-18 10:00:00 INFO [AUDIT] Action=IssueToken User=admin Client=vue-client Success=True
2026-01-18 10:05:00 WARN [AUDIT] Action=ExchangeCode User=admin Client=vue-client Success=False Reason="Replay Detected"
```

### 2.2 Contextual Logs (上下文日志)

在请求处理链路中，所有日志自动附带 `RequestId`，用于关联分布式追踪。

```cpp
LOG_INFO << "Processing request"; // 输出: [ReqId: abc-123] Processing request
```

## 3. 配置与集成

### 3.1 开启 Metrics

默认情况下，Metrics Exporter 监听 `/metrics` 端点（需在 Drogon 配置文件中开启 Exporter）。

### 3.2 日志级别

建议生产环境设置 LogLevel 为 `INFO`，调试环境为 `DEBUG`。

```json
"app": {
    "log": {
        "log_level": "INFO"
    }
}
```
