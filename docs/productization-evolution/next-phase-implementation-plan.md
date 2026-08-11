# 下一阶段实施计划

> **创建日期**: 2026-08-11
> **上游规划**: [productization-evolution-plan.md](productization-evolution-plan.md)（总体路线图）
> **IAM 缺口依据**: [iam-architecture-audit.md](iam-architecture-audit.md) §四
> **当前状态**: [progress-status.md](progress-status.md)
> **性质**: 近可执行实施计划（2–4 个月窗口），非长期路线图

---

## 一、规划方法论

### 1.1 输入来源

本计划综合三个来源的优先级：

1. **演进路线图**（[evolution-plan](productization-evolution-plan.md)）：Phase 0（可信度基线）→ Phase 1（产品化 + 社区启动）→ Phase 2（企业版）的时序约束。
2. **IAM 审计缺口**（[iam-audit](iam-architecture-audit.md) §四）：P0（企业 MVP 必须）/ P1（增强）/ P2（前瞻）的分级。
3. **进行中工作**（[progress-status](progress-status.md)）：#42 缓存层、#43 授权模型、benchmark M2–M4 的当前状态。

### 1.2 优先级裁定原则

| 原则 | 来源 | 应用 |
|------|------|------|
| **证据先于传播** | 演进方案 §二原则 1 | Phase 0 benchmark 必须在对外传播之前完成 |
| **低垂果实优先** | IAM 审计 §四 P0 评估 | 用户管理补全（1–2 周）优先于同等 P0 但工程量大的 SAML/LDAP |
| **阻塞解除优先** | 依赖分析 | #41 spec bug 阻塞 Phase 1 全部客户端工作 → 先修 |
| **内部健康度** | 架构债务 | #42/#43 已有设计稿，趁热打铁比重启设计成本低 |
| **诚实声明** | 演进方案 §二 | 不在无数据时做性能声明；不在缺功能时宣称完整 |

### 1.3 不做什么（明确排除）

- **SAML 2.0 / LDAP / SCIM**：工程量大（合计 6–11 月），且需要客户驱动的最小子集先行。本期不启动，留到企业版需求确认后。
- **云托管（Phase 3）**：未达启动门槛。
- **ABAC / 风控引擎**：P2 级，远期。
- **异步回调重构**：评估报告结论为"暂缓"，本期维持决策。

---

## 二、下一阶段工作项（按优先级排序）

### 第一梯队：低垂果实 + 阻塞解除（立即启动，1–3 周）

这些工作项成本低、影响大、无外部依赖，应立即启动。

#### A1. 用户管理补全 ★ 最高性价比

> **IAM 审计**: §四 P0「用户管理补全（创建/删除/分页/搜索）」
> **工程量**: 1–2 周 | **阻塞**: 无 | **验证**: 2026-08-11 二次验证确认仍缺失

**现状（二次验证 2026-08-11）**:
- `UserAdminService.cc:82-84` — `listUsers` 全量查询，无分页（对比：`AuditService.cc:122` 和 `TokenManagementService.cc:129` 已有 `paginate()`）
- 无 `createUser`（UserAdminController.h 无 `POST /api/admin/users` 路由）
- 无 `deleteUser`（无 DELETE 路由；仅有 `disableUser` 设 `locked_until=9999999999`）
- 无搜索/过滤（`listUsers` 不读 query params）
- `updateUser` 仅 2 字段（`email` + `email_verified`）
- ⚠️ `UserAdminController.cc:26` OpenAPI 描述谎称 "paginated"——应修正

**实施步骤**:
1. **分页**：`UserAdminService::listUsers` 加 `page`/`per_page` 参数 + `mapper.paginate()`，复用 `AuditService` 的分页模式
2. **搜索/过滤**：加 `q`（用户名/邮箱模糊）、`role`、`locked` 查询参数 + `Criteria` 构建
3. **创建用户**：新增 `POST /api/admin/users`（service `createUser` + controller 路由 + 审计日志）
4. **删除用户**：新增 `DELETE /api/admin/users/{userId}`（软删除 `deleted_at` 或硬删除 + 级联吊销 token）
5. **updateUser 扩展**：增加 `username`、`mfa_enabled`、`locked`、`org_id` 字段
6. **修正 OpenAPI**：`UserAdminController.cc:26` 描述改准确
7. **测试**：集成测试覆盖新建 CRUD + 分页 + 搜索

**验收标准**:
- `GET /api/admin/users?page=2&per_page=20&q=alice&role=user` 返回分页元数据 + 过滤结果
- `POST /api/admin/users` 创建用户（含密码哈希 + 默认角色 + 审计日志）
- `DELETE /api/admin/users/{userId}` 删除（含 token 级联吊销）
- 现有测试保持 green

#### A2. 关闭 OAuth/OIDC 审计追踪 issue ★ 收尾

> **追踪 issue**: #40 | **工作量**: 30 分钟

全部 31 项发现 + 5 项复扫发现已修复（PR #44 合并）。#40 待人工关闭（fine-grained PAT 缺 close 权限）。

**行动**: 关闭 #40；可选追加一条总结评论链接到 [done/oauth-oidc-compliance-audit.md](done/oauth-oidc-compliance-audit.md)。

#### A3. 修复 #41 OpenAPI spec bug ★ 解除客户端 SDK 阻塞

> **Issue**: #41 | **工作量**: 1–2 天

security 字段 object→array bug 曾修于 `fix/openapi-security-field-41` 分支；clientCredentialsAuth 缺失已修（`7a8473e`）。需确认合并状态、若未合并则合并。

**行动**: 确认分支状态 → 合并或 cherry-pick → 关闭 #41。此为 Phase 1 客户端 SDK 的前置。

---

### 第二梯队：进行中架构改进的推进（2–6 周）

这些工作项已有完成的设计稿，趁热打铁实现比将来重启成本低。

#### B1. #43 资源-作用域授权模型 — 实现

> **设计文档**: [in-progress/resource-scope-authorization-design.md](in-progress/resource-scope-authorization-design.md)
> **工程量**: 中（2–3 周） | **前置**: 设计已完成

**设计要点**（已验证 file:line）:
- 单一声明式 `(path, method) → required-scopes` 注册表，替换当前 3 个并行注册表
- 读写粒度：`<resource>:read` / `<resource>:write`
- Scope implication：admin 隐含 leaf scope（当前 `ScopeChecker.h:22-44` 不支持）
- 移除三重硬编码 admin scope list（`IdentityService.cc:227-240`、`ScopeDecisionEngine.cc:6-23`），改由 DB `requires_admin_role` 驱动
- 统一 `insufficient_scope` 错误路径（当前 3 个发射点不一致）

**实施步骤**:
1. 定义 `ScopeRegistry`（声明式注册表数据结构 + 加载机制）
2. 改造 `OAuth2AuthFilter` + `AuthorizationFilter` 从 registry 查询 scope
3. 实现 `ScopeChecker` implication 层（DAG 或 implication table）
4. 统一错误路径到 `ErrorResponder`
5. 迁移三重硬编码到 DB `requires_admin_role`
6. 暴露 scope→resource matrix（admin API + OpenAPI 扩展）
7. 集成测试 + scope 交集/隐含场景

#### B2. #42 缓存层 Phase 2 — token cache

> **设计文档**: [in-progress/postgres-redis-cache-design.md](in-progress/postgres-redis-cache-design.md) §10.2
> **工程量**: 中（2–3 周） | **前置**: Phase 1 已交付

**设计要点**（N1–N3 约束已分析）:
- `RedisCachedTokenRepository`：`getAccessToken` + `introspectToken` 缓存
- **N2 约束**：`introspectToken` 回落到 refresh_tokens 表（`PostgresTokenRepository.cc:548-587`）→ 需 access-vs-refresh 判别器，避免盲缓存 refresh-token introspection
- **N3 约束**：`revokeAccessToken` 只收到 hashed token（无 exp）→ 负缓存固定 60s TTL
- revoke invalidation：`revokeAccessToken` / `revokeRefreshToken` / `revokeTokenFamily` 必须失效缓存条目

**实施步骤**:
1. 实现 token-introspection 缓存（含 access-vs-refresh 判别器）
2. 实现 revoke invalidation（3 个撤销路径的缓存失效）
3. 实现 access-token negative cache（固定 60s TTL）
4. 配置化（`cache.token_ttl`）
5. 集成测试（缓存命中/穿透/失效/负缓存）
6. Metrics 计数器（hit/miss/eviction）

---

### 第三梯队：Phase 0 benchmark 推进（3–6 周，关键路径）

> **设计文档**: [in-progress/benchmark-facility-design.md](in-progress/benchmark-facility-design.md)
> **阻塞**: #45（docker compose 路径）、#46（迁移竞态）需先 workaround

这是 Phase 1 的**关键路径**——没有 Phase 0 数据，所有对外传播都阻塞。

#### C1. benchmark M2–M3（S3–S6 场景）

| 场景 | 端点 | 关键约束 |
|------|------|----------|
| S3 introspect | `POST /oauth2/introspect` | 必须用 active token（malformed = 快速返回，虚高吞吐） |
| S4 auth_code+PKCE | `POST /oauth2/token` (grant=auth_code) | PKCE 强制开启（F-011），必须发 code_challenge |
| S5 refresh_token | `POST /oauth2/token` (grant=refresh) | 每 VU 独立 RT 池，每个 RT 只用一次（V008 family 旋转） |
| S6 userinfo | `GET /oauth2/userinfo` | 需有效 access_token |

**环境约束**（benchmark 修正记录）:
- 必须用 **postgres+redis 全栈**，非 memory 模式（memory 无用户存储）
- seed 必须显式 `psql -f seed/*.sql`（docker initdb 不递归子目录）
- seed 用户不能是 admin/admin（渐进式锁定 5/10/15/20 次失败 → 1m/5m/30m/1h）

**实施步骤**:
1. 先 workaround #45（absolute-path compose override）和 #46（直接 psql 跑迁移）
2. 实现 S3–S6 Lua 脚本（按 benchmark 设计的阶梯加压方法）
3. run-scenario.sh 扩展支持 S3–S6
4. 512 bench 用户 seed 已就绪（`apps/server/seed/bench_users.sql`）

#### C2. benchmark M4 — 承重假设验证报告

**验收**: 对照 [research.md](productization-research.md) §3.1 四个数字逐条标注"实测达成 / 未达成 / 修正为 X"：
- 单机 QPS ~10万+
- 内存 50–120MB
- P99 <2ms
- 冷启动 ~5s

**输出**: 首次数据落盘到 `benchmarks/results/`，生成承重假设验证报告。

**后续行动**: 据实测结果修订 research.md §3.1 表格（标注"实测 / 目标"）。**诚实优于夸大**——若某维度不领先，收敛卖点。

---

### 第四梯队：IAM 补齐（可与梯队三并行，4–8 周）

#### D1. Backchannel Logout 真实实现

> **IAM 审计**: §四 P0 | **工程量**: 中（1 月）

**现状**: `IdentityAssembly.cc:49-57` `LoggingBackchannelLogoutNotifier` 仅 LOG_DEBUG。DB 层已有 `backchannel_logout_uri` / `backchannel_logout_session_required` 字段（V015 迁移 + ORM）。

**实施步骤**:
1. 查询 client 的 `backchannel_logout_uri`
2. 构造 `logout_token` JWT（OIDC Back-Channel Logout 1.0 draft）：sub/aud/iat/jti/events
3. HTTP POST 到每个 RP 的 backchannel_logout_uri（含超时/重试）
4. 集成测试

#### D2. 社交账号 link/unlink

> **IAM 审计**: §四 P1 | **工程量**: 小（3–5 天）

**现状**: `ISocialAccountRepository` 接口存在（`identity/include/.../ISocialAccountRepository.h`），但仅被 GitHubAuthService 内部 find-or-create 消费，未暴露为 REST 端点。

**实施步骤**:
1. 新增 `POST /api/me/social/links/{provider}`（发起 OAuth 关联流）
2. 新增 `DELETE /api/me/social/links/{provider}`（解除关联）
3. `UserSelfServiceController` 路由 + service 方法
4. 集成测试

#### D3. WebAuthn attestation/assertion 签名验证

> **IAM 审计**: §四 P1 | **工程量**: 中（2–3 周）

**现状**: `WebAuthnService.cc:58-103` `finishRegistration` 仅存储 credentialId/publicKey；`:121-161` `finishAuthentication` 仅查 credential + 更新 signCount。

**实施步骤**:
1. 引入 CBOR 解码（当前项目无 CBOR 库——需评估 libcbor 或手写最小解码器）
2. attestation 签名验证（注册流程）
3. assertion 签名验证（认证流程）
4. AAGUID 认证器认证（可选——仅高合规场景需要）
5. 集成测试（需真实 WebAuthn 设备或模拟器）

> ⚠️ **安全说明**：当前简化实现在安全上是**可接受的**（浏览器已验证 attestation），但不满足需要服务端验证认证器可信度的高合规场景。本期是否做取决于目标客户需求。

---

## 三、时间线建议

### Sprint 1（第 1–2 周）：低垂果实 + 阻塞解除

| 工作项 | 优先级 | 产出 |
|--------|--------|------|
| A1 用户管理补全 | P0 | 分页/搜索/创建/删除 + 测试 |
| A2 关闭 #40 | P0 | 审计追踪收尾 |
| A3 修复 #41 | P0 | spec bug 修复 + 关闭 issue |

### Sprint 2–3（第 3–6 周）：架构改进推进

| 工作项 | 优先级 | 产出 |
|--------|--------|------|
| B1 #43 授权模型实现 | P0 | 声明式 scope 注册表 + implication + 统一错误 |
| B2 #42 token cache Phase 2 | P1 | introspectToken 缓存 + invalidation + 负缓存 |
| C1 benchmark M2–M3（启动） | P1 | S3–S6 场景脚本（需先 workaround #45/#46） |

### Sprint 4–6（第 7–12 周）：benchmark 收尾 + IAM 补齐

| 工作项 | 优先级 | 产出 |
|--------|--------|------|
| C1 benchmark M2–M3（完成） | P0 | S3–S6 全部验证 green |
| C2 benchmark M4 承重报告 | P0 | 数据落盘 + 假设验证 + research.md 修订 |
| D1 Backchannel Logout | P0 | 真实 HTTP POST + logout_token JWT |
| D2 社交账号 link/unlink | P1 | 2 个端点 + 测试 |

### Sprint 7–8（第 13–16 周）：进入 Phase 1 准备

Phase 0 数据落地后解锁 Phase 1 工作：

| 工作项 | 优先级 | 前置 |
|--------|--------|------|
| OpenAPI spec 治理（Layer 1） | P0 | #41 已修 |
| 独立文档站 | P0 | — |
| Python/Go 客户端 SDK | P0 | spec 治理完成 |

---

## 四、依赖关系图

```
A3 (#41 spec bug)
  └─→ OpenAPI spec 治理 (Phase 1 Layer 1)
        └─→ Python/Go 客户端 SDK (Phase 1 Layer 2)

C1 (#45/#46 workaround)
  └─→ benchmark M2–M3 (S3–S6)
        └─→ benchmark M4 (承重报告)
              └─→ README 性能徽章
              └─→ 技术博客 + 基准报告发布
              └─→ TechEmpower 提交
              └─→ research.md §3.1 修订

B1 (#43 授权模型)     ← 设计已完成，可立即开始
B2 (#42 token cache)  ← 设计已完成，Phase 1 已交付

A1 (用户管理)          ← 无依赖，可立即开始
D1 (Backchannel Logout) ← 无依赖
D2 (社交 link/unlink)  ← 无依赖
```

**关键路径**: #41 → spec 治理 → 客户端 SDK（Phase 1 的阻塞链）
**关键路径**: benchmark M2–M3 → M4 → 对外传播（Phase 0 → Phase 1 的阻塞链）

---

## 五、风险与缓解

| 风险 | 概率 | 影响 | 缓解 |
|------|------|------|------|
| **benchmark 实测证伪承重假设**（如 P99 >2ms） | 高 | 高（核心叙事受损） | 演进方案 §二已预案：收敛卖点到真正领先维度；**诚实优于夸大** |
| #45/#46 阻塞 benchmark 无法跑 | 中 | 高 | absolute-path compose override 已有 workaround；直接 psql 跑迁移绕过 SchemaManager |
| CBOR 库引入增加构建复杂度（D3） | 中 | 中 | 评估是否本期必须——若目标客户无高合规需求，defer |
| 用户管理删除的数据完整性（A1） | 低 | 中 | 软删除（`deleted_at`）优于硬删除；级联吊销 token 复用已有 `revokeAll` |
| SAML/LDAP/SCIM 客户需求突然出现 | 低 | 高 | 保持设计前置评估能力；本期明确排除，避免分散精力 |

---

## 六、验收检查清单

每个工作项完成后，确认：

- [ ] 代码通过 `manage.ps1 build-backend` + `test-backend`（全绿）
- [ ] CI 门全部 green（api-diff / migration-check / openapi_spec_validator）
- [ ] OpenAPI YAML 更新（如涉及 HTTP 面变更）
- [ ] 集成测试覆盖新功能 + 回归测试保持 green
- [ ] [progress-status.md](progress-status.md) 更新工作项状态
- [ ] 相关 GitHub issue 更新（关闭/评论）
- [ ] 如有 BREAKING 变更：api-diff `--force` ratify 或 SemVer bump 决策

---

## 七、决策待确认项

以下决策点需在启动前与需求方确认：

| # | 决策 | 选项 | 建议 |
|---|------|------|------|
| 1 | A1 用户删除策略 | 软删除（`deleted_at`）vs 硬删除 | **软删除**（GDPR 合规 + 可恢复） |
| 2 | A1 分页参数风格 | `page/per_page` vs `offset/limit` vs `cursor` | **`page/per_page`**（与 AuditService 一致） |
| 3 | D3 WebAuthn 是否本期做 | 本期做 vs defer 到企业需求确认 | **defer**——除非目标客户明确需要服务端 attestation 验证 |
| 4 | C2 承重假设被证伪后的卖点策略 | 收敛维度 vs 修正数字 vs 下修目标 | **收敛维度 + 诚实修正**（演进方案 §二预案） |

---

*本计划是 2–4 个月窗口的近可执行实施计划。长期路线图见 [productization-evolution-plan.md](productization-evolution-plan.md)。当前状态见 [progress-status.md](progress-status.md)。*
