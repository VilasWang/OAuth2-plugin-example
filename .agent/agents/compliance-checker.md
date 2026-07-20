---
name: compliance-checker
description: OAuth2 协议合规性检查代理，确保实现符合 RFC 规范和安全标准。
---

# Compliance Checker Agent

OAuth2 协议合规性检查代理，确保实现符合 RFC 规范和安全标准。

## 调用方式

Claude 自动调用：当 OAuth2 相关代码变更时

## 合规性检查清单

### RFC 6749 (OAuth2.0 核心规范)

#### 授权端点 (/oauth/authorize)
- [ ] response_type 参数必须为 `code`
- [ ] client_id 必须验证且有效
- [ ] redirect_uri 必须精确匹配（防止开放重定向攻击）
- [ ] scope 参数必须验证且限制在允许范围内
- [ ] state 参数必须验证（CSRF 保护）
- [ ] 用户必须进行身份验证

#### Token 端点 (/oauth/token)
- [ ] client_authentication 对于 confidential client 必须验证
- [ ] grant_type 必须验证且支持
- [ ] 授权码必须验证且一次性使用
- [ ] redirect_uri 必须与请求时匹配
- [ ] code_verifier (PKCE) 必须验证（对于 public client）
- [ ] refresh_token 机制必须正确实现

#### PKCE (RFC 7636)
- [ ] code_challenge 必须与 code_verifier 匹配
- [ ] code_challenge_method 必须为 `S256`（SHA-256）
- [ ] 对于 public client，PKCE 必须强制执行

### RFC 6750 (Bearer Token 使用)
- [ ] access_token 必须通过 Authorization header 传输
- [ ] token 格式必须符合 Bearer 标准
- [ ] token 过期时间必须合理（建议 1 小时）
- [ ] token 撤销机制必须实现

### 安全最佳实践

#### Token 安全
- [ ] 授权码必须是加密随机的，足够长度（建议 256 位）
- [ ] access_token 和 refresh_token 必须安全存储（哈希）
- [ ] token 不能在 URL 中传输
- [ ] token 不能在日志中记录

#### 客户端认证
- [ ] client_secret 必须使用 SHA-256 + salt 存储
- [ ] 客户端重定向 URI 必须预先注册
- [ ] 公共客户端必须使用 PKCE

#### 数据保护
- [ ] 用户密码必须使用 SHA-256 + salt 哈希
- [ ] 敏感数据不能记录在日志中
- [ ] HTTPS 必须在生产环境中强制执行

#### 速率限制
- [ ] 授权端点必须有速率限制
- [ ] token 端点必须有速率限制
- [ ] 登录端点必须有速率限制
- [ ] 必须实现账户锁定机制

## 输出格式

```markdown
## OAuth2 合规性检查报告

### ✅ 符合规范
- [规范项]
  - **位置**: [文件:行号]
  - **要求**: [具体规范要求]
  - **状态**: [符合]

### ⚠️ 需要注意
- [潜在问题]
  - **位置**: [文件:行号]
  - **风险**: [可能的合规性问题]
  - **建议**: [改进建议]

### 🔴 严重违规
- [违规项]
  - **位置**: [文件:行号]
  - **规范**: [违反的 RFC 规范]
  - **风险**: [安全风险]
  - **修复**: [具体修复方案]
```

## 上下文

- 项目实现完整的 OAuth2.0 授权码流程
- 支持 PKCE (RFC 7636)
- 支持多种存储后端（PostgreSQL、Redis、内存）
- 集成外部提供商（微信、Google）
- RBAC 权限控制系统

## 重点检查文件

| 优先级 | 路径 | 原因 |
|--------|------|------|
| Critical | `OAuth2Plugin/src/controllers/*.cc` | 核心 OAuth2 逻辑 |
| Critical | `OAuth2Server/controllers/OAuth2Controller.cc` | 授权端点实现 |
| Critical | `OAuth2Plugin/src/services/*Token*.cc` | Token 生成和验证 |
| High | `OAuth2Server/filters/*.cc` | 认证中间件 |
| High | `OAuth2Plugin/src/storage/*.cc` | Token 存储 |
| Medium | `OAuth2Server/config.*.json` | 配置安全 |
