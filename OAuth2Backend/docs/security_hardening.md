# Security Hardening Guide (Phase 22)

本文档详细说明了 OAuth2 服务的安全加固措施，包括速率限制 (Rate Limiting) 和 安全响应头 (Security Headers)。

## 1. 速率限制 (Rate Limiting)

为了防止暴力破解和 DoS 攻击，系统实现了应用层速率限制。

### 1.1 机制设计

* **过滤器**: `RateLimiterFilter`
* **算法**: 固定窗口计数器 (Fixed Window Counter/Memory)
* **识别策略**:
    1. 优先读取 `X-Forwarded-For` 头 (取第一个 IP)。
    2. 其次读取 `X-Real-IP` 头。
    3. 最后降级使用 `Peer IP` (Direct Connection)。
    *这确保了在 Nginx 等反向代理后仍能正确限制真实客户端 IP。*

### 1.2 限制规则

| 接口 (Path) | 方法 | 限制 (Requests/Minute) | 触发响应 |
| :--- | :--- | :--- | :--- |
| `/oauth2/login` | POST | **5** | `429 Too Many Requests` |
| `/oauth2/token` | POST | **10** | `429 Too Many Requests` |
| `/api/register` | POST | **5** | `429 Too Many Requests` |
| *所有其他接口* | * | *无限制 (默认 60)* | - |

### 1.3 启用方式

在 `OAuth2Controller.h` 中通过 `ADD_METHOD_TO` 宏注册：

```cpp
ADD_METHOD_TO(OAuth2Controller::login, "/oauth2/login", Post, "RateLimiterFilter");
```

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
