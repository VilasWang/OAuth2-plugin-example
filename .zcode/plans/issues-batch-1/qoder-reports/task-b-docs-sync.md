# 任务 B：文档同步

## B.1：api-reference.md — end_session id_token_hint 描述更新

### 改动位置

`docs/domains/api-reference.md` 第 3.1 节 RP-Initiated Logout 端点。

### 改动内容

**参数表 `id_token_hint` 行**（原第 226 行）：
- 补充 `aud` 支持字符串或数组（RFC 7519 §4.1.3），服务端逐一尝试候选项校验 `post_logout_redirect_uri`
- 验签失败返回 400 Error Envelope，错误码 `AUTH_INVALID_ID_TOKEN_HINT`（4006）
- 未注册的 `post_logout_redirect_uri` 返回 400 Error Envelope，错误码 `VALIDATION_REDIRECT_URI_NOT_REGISTERED`（3013）
- 未提供 `id_token_hint` 时返回 `AUTH_INVALID_ID_TOKEN_HINT`（4006）

**响应 400 描述**（原第 234 行）：
- 更新为引用具体错误码：`VALIDATION_REDIRECT_URI_NOT_REGISTERED`（3013）和 `AUTH_INVALID_ID_TOKEN_HINT`（4006）

### 与 openapi.yaml 一致性

与已改的 openapi.yaml 语义一致（数组 aud 支持、新错误码 3013）。

---

## B.2：deployment.md — 社交账号绑定 Redis 依赖

### 改动位置

`docs/operate/deployment.md` 运维操作章节，audit_logs 分区维护之后、故障排除之前。

### 新增内容

新增 `### 社交账号绑定（#71）Redis 依赖` 小节：
- link state 存 Redis（`SET NX EX 600` / `GETDEL`，TTL 10 分钟）
- 有 Redis 时正常工作
- 无 Redis 时绑定发起端点 fail-closed，返回 `NotConfigured` 类错误（HTTP 503）
- 登录不受影响（已绑定账号的直接登录不依赖 Redis link state）

## 状态

- [x] 两处文档改动已完成
