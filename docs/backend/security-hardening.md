# Security Hardening Guide (Phase 22)

本文档详细说明了 OAuth2 服务的安全加固措施，包括速率限制 (Rate Limiting) 和 安全响应头 (Security Headers)。

## 1. 速率限制 (Rate Limiting)

为了防止暴力破解和 DoS 攻击，系统使用 Drogon 官方的 Hodor 插件实现应用层速率限制。

### 1.1 机制设计

* **插件**: `drogon::plugin::Hodor`
* **算法**: 令牌桶 (Token Bucket)
* **识别策略**:
    1. 优先读取 `X-Forwarded-For` 头（取第一个 IP）。
    2. 其次读取 `X-Real-IP` 头。
    3. 最后降级使用 `Peer IP`（直连 IP）。
    4. 白名单 IP（127.0.0.1, Docker 网络）完全跳过限制。

### 1.2 限制规则

| 接口 (Path) | 方法 | 全局限制 | 每IP限制 | 每用户限制 | 触发响应 |
| :--- | :--- | :--- | :--- | :--- | :--- |
| `/oauth2/login` | POST | 5000/min | **5/min** | 5/min | `429 Too Many Requests` |
| `/oauth2/token` | POST | 10000/min | **10/min** | 10/min | `429 Too Many Requests` |
| `/api/register` | POST | 5000/min | **5/min** | 5/min | `429 Too Many Requests` |
| *所有其他接口* | * | 1000/min | 60/min | 无限制 | - |

### 1.3 配置方式

Hodor 插件通过 `config.json` 配置，支持动态调整限制参数而无需重新编译。

详细配置参见 `docs/history/design/superpowers/specs/2026-04-13-hodor-rate-limiter-migration-design.md`（已归档）。

---

## 2. 安全响应头 (Security Headers)

为了防御常见的 Web 攻击（如 MIME 嗅探、点击劫持），系统强制添加了现代 Web 安全头。

### 2.1 机制设计

使用 Drogon 的 **Global Post Handling Advice** 在所有 HTTP 响应（包括 404/500）中注入安全头。

### 2.2 响应头列表

| Header | Value | 作用 |
| :--- | :--- | :--- |
| `X-Content-Type-Options` | `nosniff` | 禁止浏览器猜测 MIME 类型，防止 XSS。 |
| `X-Frame-Options` | `SAMEORIGIN` | 仅允许同源站点嵌入 iframe，防止点击劫持。 |
| `Content-Security-Policy` | `default-src 'self'; frame-ancestors 'self';` | 限制资源加载源，基础 CSP 策略。 |
| `Strict-Transport-Security` | `max-age=31536000; includeSubDomains` | 强制 HTTPS 连接 (HSTS)。 |

### 2.3 验证

可以使用 `curl -I` 命令验证：

```bash
curl -I http://localhost:5555/
# Output should contain:
# x-frame-options: SAMEORIGIN
# x-content-type-options: nosniff
# ...
```

---

## 3. 安全测试验证 (Security Testing Verification)

所有安全加固措施均已通过全面的自动化测试验证（2026-04-21）。

### 3.1 测试覆盖范围

- [PASS] **输入验证测试** (7/7): SQL 注入、XSS、命令注入、输入长度限制、空值验证
- [PASS] **认证授权测试** (2/2): 无效凭据、速率限制
- [PASS] **CORS 配置测试** (2/2): 授权源访问、未授权源拒绝
- [PASS] **敏感数据处理测试** (2/2): POST Body 传递、URL 参数后备兼容性
- [PASS] **Token 安全测试** (3/3): 无效授权码、缺失授权码、无效 Refresh Token
- [PASS] **安全头测试** (2/2): 基础安全头、HSTS 配置
- [PASS] **速率限制测试** (1/1): 暴力破解防护
- [PASS] **健康检查安全测试** (1/1): 信息泄露检查

**总计**: 18/18 测试通过 (100%) [PASS]

### 3.2 测试报告

详细的安全测试结果和测试用例请参阅：
[DOC] **Security Test Report**（本地文档，已归档）

包含内容：
- 完整的测试用例列表和执行结果
- 安全评分和特性验证
- 性能指标和测试自动化建议
- 生产环境安全评估

### 3.3 生产就绪状态

**当前安全状态**: [INFO] **生产就绪**

所有关键安全测试通过。系统在以下方面表现优秀：
- 输入验证和注入防护
- 认证和授权机制
- CORS 和安全头配置
- Token 管理和撤销
- 速率限制和 DoS 防护

### 3.4 持续安全监控

建议定期执行此安全测试套件作为回归测试：
- **每次提交**: 安全扫描 + 单元测试
- **PR 合并前**: 完整安全测试套件
- **每日**: 完整测试套件回归

---

**相关文档**:
- [Security Architecture](./security-architecture.md) - 安全架构设计
- [Data Consistency](./data-consistency.md) - 数据一致性和威胁模型
- [Testing Guide](./testing-guide.md) - 完整测试指南
- [Bug Status] - 剩余 Bug 分析和风险评估（本地文档，已归档）
