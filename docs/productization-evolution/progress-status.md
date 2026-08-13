# 产品化演进进展总览

> **更新日期**: 2026-08-12
> **维护约定**: 每次完成一个工作项或里程碑后更新本文件。开始新工作前先查本文件确认当前状态。
> **上游规划**: [productization-evolution-plan.md](productization-evolution-plan.md)（总体路线图）
> **代码依据**: [iam-architecture-audit.md](iam-architecture-audit.md)（IAM 业务能力审计）

---

## 一、总体进展概览

AuthForge 产品化演进按 [演进方案](productization-evolution-plan.md) 的 4 个 Phase 推进。截至 2026-08-12：

| Phase | 状态 | 完成度 | 说明 |
|-------|------|--------|------|
| **Phase 0** — 可信度基线 | 🟡 进行中 | ~70% | benchmark M1–M3（S1–S6 全部场景）+ M4 首版报告已交付；承重假设初步验证（S1 86k QPS、P99 低并发达成）；竞品对比（Phase 0.5）+ 冷启动/内存测量待做 |
| **Phase 1** — 产品化基础 + 社区启动 | ⬜ 未开始 | 0% | 文档站、多语言客户端 SDK（含 spec 治理地基）、技术博客均未启动 |
| **Phase 2** — 企业版 | ⬜ 未开始（设计中） | ~5% | #42 缓存层 Phase 1+2 已交付（基础设施类）；#43 授权模型草案完成；SAML/LDAP/SCIM 完全空白 |
| **Phase 3** — 云托管 | ⬜ 未启动 | 0% | 未达启动门槛（自托管付费客户 ≥ N） |

**横切工作流（不绑定单一 Phase）**:

| 工作流 | 状态 | 完成度 | 说明 |
|--------|------|--------|------|
| OAuth/OIDC 合规审计 | ✅ 已完成 | 100% | 31 项偏差（F-001..F-031）+ 5 项复扫发现全部处置（PR #44 合并 2026-08-09） |
| 架构改进（#42 缓存层） | 🟡 进行中 | ~50% | Phase 1（client-cache）已交付（PR #47）；Phase 2（token cache）已交付（commit 8765ad7）；Phase 3/4 未开始 |
| 架构改进（#43 授权模型） | 🟡 设计中 | ~15% | 完整设计方案完成，待实现 |
| 异步回调评估 | ✅ 已完成（评估） | 100% | 评估报告产出；决策：暂不动手（C++17 锁定，协程排除） |

---

## 二、按工作项的详细状态

### 2.1 Phase 0 — 可信度基线

> **设计文档**: [in-progress/benchmark-facility-design.md](in-progress/benchmark-facility-design.md)
> **目标**: 把性能断言变成可复现的测量

| 工作项 | 状态 | 完成证据 / 阻塞 | 下一步 |
|--------|------|----------------|--------|
| **M1: 基准设施骨架** | ✅ 已完成 | `benchmarks/` 目录已建：setup/teardown/run-scenario + S1(discovery) + S2(client_credentials) Lua 脚本 + parse-wrk.py + seed/bench_users.sql（512 用户）；WSL 实测 S1 ~14.7k QPS / S2 1.2k→3.1k QPS，0% 错误 | — |
| **M1 附属: 修正虚构 CI 报告** | ✅ 已完成 | `.github/workflows/_build-test.yml` 的装饰性 Performance Report 步骤已移除 | — |
| **M2: S3 introspect + S4 auth_code 场景** | ✅ 已完成 | `lib/gen-tokens.py`（token+PKCE 生成）+ `s3-introspect.lua`（active token，backend-svc Basic auth）+ `s4-auth-code.lua`（多步 login→token，每 VU 独立用户，S256 PKCE 预生成，强制 -t==-c）；WSL 8vCPU 实测 S3 17k QPS / S4 465 QPS | — |
| **M3: S5 refresh_token + S6 userinfo + 资源观测** | ✅ 已完成 | `s5-refresh-token.lua`（一次性 RT 池，`--reseed` 每档刷新）+ `s6-userinfo.lua`（bearer 用户 AT）+ `observe/` 脚本 + `measure-cold-start.sh` + 基准专用 `config.bench.json`（PG=25/Redis=20）+ `docker-compose.bench.yml`；WSL 实测 S5 2k QPS / S6 17k QPS | — |
| **M4: 承重假设验证报告** | ✅ 首版已完成 | `benchmarks/results/SUMMARY.md`：6 场景阶梯数据 + 承重验证（S1 86k QPS ⚠️接近 10万；P99 低并发达成 <2ms ✅；内存 ❌需重定义；冷启动 ⏳待测）+ 40 个 JSON 结果入仓 | 冷启动/内存精确测量；竞品对比（Phase 0.5） |
| **Phase 0.5: 竞品对比** | ⬜ 未开始 | — | Keycloak/Ory/Zitadel 同环境压测 |
| **结果入仓** | ✅ 已完成 | `benchmarks/results/` 含 40 个 JSON（S1–S6 × 2–128 并发档）+ SUMMARY.md | — |

**已知环境问题（曾阻塞 benchmark，现已修复）**:
- **#45**: docker compose v5.3.1 相对路径解析错误 → 已修复（e37e627，auto-generate absolute-path compose override）
- **#46**: SchemaManager 冷启动迁移竞态（V3 看不到 V2 的已提交表） → 已修复（6ffcd27，单事务完成全部 migration）

### 2.2 Phase 1 — 产品化基础 + 社区启动

| 工作项 | 状态 | 完成证据 / 阻塞 | 下一步 |
|--------|------|----------------|--------|
| **独立文档站**（Docusaurus/VitePress） | ⬜ 未开始 | — | 选型 + 立项 |
| **OpenAPI spec 治理**（Layer 1 前置） | ⬜ 未开始 | `apps/server/openapi.yaml` 当前"路径全、内容稀疏"（D1.5）；死孤儿 `docs/backend/api/openapi.json` 已删（2026-08-12，D2） | 定 YAML 单源 + YAML↔代码一致性门 + oasdiff 门 |
| **OpenAPI bug（#41）** | ✅ 已修复 | security 字段 object→array shape 已修（de03a19，入 master）；clientCredentialsAuth 缺失已在 `7a8473e` 修；b99ef5b 进一步补登 PKCE/MFA 参数 | — |
| **Python 客户端 SDK** | ⬜ 未开始 | 阻塞于 spec 治理 | openapi-python-client 生成 + 手写 auth 层 |
| **Go 客户端 SDK** | ⬜ 未开始 | 阻塞于 spec 治理 | oapi-codegen 生成 + 手写 auth 层 |
| **README 性能徽章** | ⬜ 未开始 | 阻塞于 Phase 0 数据 | — |
| **首发技术博客 + 基准报告** | ⬜ 未开始 | 阻塞于 Phase 0 数据 | — |
| **TechEmpower 提交** | ⬜ 未开始 | 阻塞于 Phase 0 数据 | — |

### 2.3 Phase 2 — 企业版（IAM 缺口）

> **缺口依据**: [iam-architecture-audit.md](iam-architecture-audit.md) §四业务缺口优先级

#### P0（企业版 MVP 必须）

| 缺口 | 状态 | 代码依据（IAM 审计） | 工程量估算 |
|------|------|---------------------|-----------|
| **用户管理补全**（创建/删除/分页/搜索） | ⬜ 未开始 | 确认缺失：UserAdminService 无 createUser/deleteUser/pagination/search（2026-08-11 二次验证仍为真） | 小（1–2 周） |
| **Backchannel Logout 真实实现** | 🟡 后端已交付 | 真实通知器 + logout_token 构造 + admin API 配置 + discovery + 单测（D1-D6）；前端就绪但被 Mimosa 拦截（mock-api.ts 既有误报）；集成测试待补（需 PG）。分支 `feat/backchannel-logout-b1`，详见 [in-progress/backchannel-logout-design.md](in-progress/backchannel-logout-design.md) | 中（1 月） |
| **#42 缓存层** | 🟡 Phase 1 已交付 | client-cache decorator 已合并（PR #47）；token cache Phase 2 设计完成 | — |
| **#43 授权模型** | 🟡 设计完成 | 完整 `(path,method)→scope` 声明式注册表 + scope implication 设计 | 中 |
| **SAML 2.0** | ⬜ 未开始 | 穷尽搜索 0 代码文件 | 大（3–6 月，需 XML 签名库） |
| **LDAP/AD 联邦** | ⬜ 未开始 | 穷尽搜索 0 代码文件 | 中（2–3 月） |
| **SCIM 2.0** | ⬜ 未开始 | 穷尽搜索 0 代码文件 | 中（1–2 月） |
| **多租户隔离激活** | ⬜ 未开始 | schema + Organization API 有（V017），请求级隔离无（TenantId.h 声明"does not implement real isolation"） | 大 |

#### P1（企业版增强）

| 缺口 | 状态 | 代码依据 | 工程量估算 |
|------|------|----------|-----------|
| WebAuthn attestation/assertion 验证 | ⬜ 未开始 | 简化实现：`WebAuthnService.cc:58-103` 不验签 | 中 |
| ABAC 策略引擎 | ⬜ 未开始 | 穷尽搜索 0 代码 | 大 |
| 审计合规报告导出 | ⬜ 未开始 | 缺失 | 小–中 |
| 社交账号 link/unlink | ⬜ 未开始 | UserSelfServiceController 无此端点 | 小 |
| 审计完整性保护（哈希链） | ⬜ 未开始 | 缺失 | 中 |
| SIEM 集成（Syslog/CEF） | ⬜ 未开始 | 缺失 | 中 |

### 2.4 横切工作流

#### OAuth/OIDC 合规审计 ✅

| 工作项 | 状态 | 说明 |
|--------|------|------|
| 审查计划 | ✅ 已完成 | [done/oauth-oidc-compliance-audit-plan.md](done/oauth-oidc-compliance-audit-plan.md) |
| 审查执行 + 31 项发现 | ✅ 已完成 | [done/oauth-oidc-compliance-audit.md](done/oauth-oidc-compliance-audit.md) |
| Batch 0（P0: F-002/003/004/005/016） | ✅ 已修复 | commit 94b1b2a |
| Batch 1（协议正确性: F-006..F-015） | ✅ 已修复 | commit 94b1b2a |
| Batch 2（OIDC 扩展: auth_time/acr/amr/nonce/prompt/max_age/RP-Logout） | ✅ 已修复 | commit 3018f75 |
| Batch 3（硬化: F-010 scope 门 / F-018 限流 / F-019..F-031） | ✅ 已修复 | commit d8237d9 |
| 二次复扫（R-1..R-5） | ✅ 已处置 | R-1/R-4/R-5 已修（edaaf37），R-2/R-3 记录（follow-up #42/#43） |
| PR #44 合并 | ✅ 已合并 | squash-commit fb775ed → master（2026-08-09） |
| 追踪 issue #40 | 🟡 待关闭 | 全部发现已修复，待人工关闭 |

#### 架构改进 — #42 Postgres+Redis 缓存层

> **设计文档**: [in-progress/postgres-redis-cache-design.md](in-progress/postgres-redis-cache-design.md)

| Phase | 状态 | 内容 |
|-------|------|------|
| Phase 1（client-cache） | ✅ 已交付 | `RedisCachedClientRepository` cache-aside decorator + config + 4 集成测试（PR #47，commit 86bc86e + d7d837b） |
| Phase 2（token cache） | ✅ 已交付 | `RedisCachedTokenRepository`（commit 8765ad7）：getAccessToken + introspectToken 缓存（access-only，N2 判别器）+ revoke invalidation + negative cache（N3，60s TTL）；含 C1/C6/C7 review fixes |
| Phase 3（移除独立 Redis 模式） | ⬜ 未开始 | BREAKING 变更，删除 `storage_type="redis"` |
| Phase 4（consent cache + L1+L2） | ⬜ 未开始 | 远期优化 |

#### 架构改进 — #43 资源-作用域授权模型

> **设计文档**: [in-progress/resource-scope-authorization-design.md](in-progress/resource-scope-authorization-design.md)

| 工作项 | 状态 | 内容 |
|--------|------|------|
| 设计草案 | ✅ 已完成 | 单一 `(path,method)→scope` 声明式注册表 + 读写粒度 + scope implication + DB `requires_admin_role` 驱动 |
| 实现 | ⬜ 未开始 | 待立项到 `openspec/changes/` 或 `.kiro/specs/` |

#### 异步回调评估 ✅

> **评估文档**: [done/async-refactor-assessment.md](done/async-refactor-assessment.md)

| 工作项 | 状态 | 内容 |
|--------|------|------|
| 评估报告产出 | ✅ 已完成 | GitHubController.cc 7 层嵌套（564 行）、UserAdminService 35 个回调点量化 |
| 示范改造（GitHubController） | ⬜ 未开始（决策：暂缓） | 决策前提：暂不动手；协程排除（C++17 锁定） |

---

## 三、GitHub Issue 跟踪

| Issue | 标题 | 状态 | 对应工作项 |
|-------|------|------|-----------|
| **#40** | OAuth/OIDC Compliance Audit 追踪 | 🟡 待关闭 | 全部发现已修复，待人工关闭 |
| **#41** | OpenApiGenerator security 字段 bug | ✅ 已修复 | de03a19（security 字段 shape）+ b99ef5b（PKCE/MFA 参数） |
| **#42** | Postgres + Redis 缓存层 | 🟡 开放 | Phase 1+2 已交付，Phase 3/4 未开始 |
| **#43** | 资源-作用域授权模型 | 🟡 开放 | 设计完成，待实现 |
| **#45** | docker compose v5.3.1 路径解析 | ✅ 已修复 | e37e627（auto-generate absolute-path override） |
| **#46** | SchemaManager 冷启动迁移竞态 | ✅ 已修复 | 6ffcd27（单事务完成全部 migration） |

---

## 四、已关闭里程碑（历史记录）

| 日期 | 里程碑 | 提交 / PR |
|------|--------|----------|
| 2026-08-09 | OAuth/OIDC 合规审计 — 31 项偏差全部修复 | PR #44 (fb775ed) |
| 2026-08-11 | #42 缓存层 Phase 1 — client-cache decorator | PR #47 (86bc86e, d7d837b) |
| 2026-08-12 | #41/#45/#46 修复 + #42 缓存层 Phase 2 token cache | de03a19, 6ffcd27, e37e627, 8765ad7 |
| 2026-08-12 | benchmark 设施 M2–M3 — S3/S4/S5/S6 场景脚本 + token 生成 + 资源观测 | feat/benchmark-m2-m3-s3-s6 |
| 2026-08-12 | benchmark M4 首版 — config.bench.json + 40 JSON 入仓 + SUMMARY.md 承重验证报告 | feat/benchmark-m2-m3-s3-s6 |
| 2026-08-09 | benchmark 设施 M1 — skeleton + S1/S2 验证 green | 0d54bbd, 518d3e3, ac832ac |
| 2026-08-05 | 产品化演进方案 + benchmark/client-sdk 设计文档 | a6d570c |

---

*本文件是活文档，随工作进展持续更新。最新状态始终以此文件为准。*
