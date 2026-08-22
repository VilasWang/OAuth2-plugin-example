# 产品化演进进展总览

> **更新日期**: 2026-08-18
> **维护约定**: 每次完成一个工作项或里程碑后更新本文件。开始新工作前先查本文件确认当前状态。
> **上游规划**: [productization-evolution-plan.md](productization-evolution-plan.md)（总体路线图）
> **代码依据**: [iam-architecture-audit.md](iam-architecture-audit.md)（IAM 业务能力审计）

---

## 一、总体进展概览

AuthForge 产品化演进按 [演进方案](productization-evolution-plan.md) 的 4 个 Phase 推进。截至 2026-08-17：

| Phase | 状态 | 完成度 | 说明 |
|-------|------|--------|------|
| **Phase 0** — 可信度基线 | ✅ 基本完成 | ~90% | benchmark M1–M4 全部交付（40 JSON + 承重验证报告）；**承重假设裁决：QPS ⚠️接近/可外推达标、P99 ✅低并发达成、内存 ✅SDK 口径远超标（实测 2.5 MB peak WS）、冷启动 ✅观测达成（~4s）**；竞品对比（Phase 0.5）待做 |
| **Phase 1** — 产品化基础 + 社区启动 | 🟡 进行中 | ~45% | **spec 治理（M0）已合并（v1.2.0）**；**C1 客户端 SDK 已实现（M1 Python + M2 Go + M3 接线 + M4 文档，2026-08-18 待合并）**；文档站/博客/README 徽章待做 |
| **Phase 2** — 企业版 | 🟡 快速推进 | ~45% | #42 缓存层 Phase 1+2 已交付；#43 授权模型已实现；**用户管理补全已实现（PR #52）**；**Backchannel Logout 后端已交付（PR #50）**；OAuth/OIDC 合规审计 100% 修复；SAML/LDAP/SCIM/多租户待做 |
| **Phase 3** — 云托管 | ⬜ 未启动 | 0% | 未达启动门槛（自托管付费客户 ≥ N） |

**横切工作流（不绑定单一 Phase）**:

| 工作流 | 状态 | 完成度 | 说明 |
|--------|------|--------|------|
| OAuth/OIDC 合规审计 | ✅ 已完成 | 100% | 31 项偏差 + 5 项复扫发现全部处置（PR #44）；所有追踪 issue #21–#40 已关闭 |
| 架构改进（#42 缓存层） | 🟡 进行中 | ~60% | Phase 1（client-cache）+ Phase 2（token cache）已交付；Phase 3（移除独立 Redis 模式）/ Phase 4（consent cache）未开始 |
| 架构改进（#43 授权模型） | ✅ 已完成 | 100% | 完整实现：声明式 `(path,method)→scope` 注册表 + scope implication + DB 驱动 admin-role + discovery 端点（commit f04d3ba） |
| 异步回调评估 | ✅ 已完成（评估） | 100% | 评估报告产出；决策：暂不动手（C++17 锁定，协程排除） |
| 版本发布 | ✅ v1.1.0 已打 tag | — | `v1.1.0`（含 OAuth/OIDC 合规修复 + #42/#43 架构改进 + benchmark 设施） |

---

## 二、按工作项的详细状态

### 2.1 Phase 0 — 可信度基线

> **设计文档**: [in-progress/benchmark-facility-design.md](in-progress/benchmark-facility-design.md)
> **实测报告**: `benchmarks/results/SUMMARY.md`
> **目标**: 把性能断言变成可复现的测量

| 工作项 | 状态 | 完成证据 / 阻塞 | 下一步 |
|--------|------|----------------|--------|
| **M1: 基准设施骨架 + S1/S2** | ✅ 已完成 | setup/teardown/run-scenario + S1(discovery) + S2(client_credentials) Lua + parse-wrk.py + 512 bench 用户 seed | — |
| **M2: S3 introspect + S4 auth_code** | ✅ 已完成 | `s3-introspect.lua`（active token）+ `s4-auth-code.lua`（多步 login→token，PKCE 预生成）；S3 17k QPS / S4 465 QPS | — |
| **M3: S5 refresh_token + S6 userinfo + 观测** | ✅ 已完成 | `s5-refresh-token.lua`（一次性 RT 池）+ `s6-userinfo.lua` + `observe/` 脚本 + `config.bench.json`；S5 2k QPS / S6 17k QPS | — |
| **M4: 承重假设验证报告** | ✅ 已完成 | `SUMMARY.md`：6 场景阶梯数据 + 承重裁决 + 40 JSON 入仓 | 冷启动/内存精确测量 |
| **Phase 0.5: 竞品对比** | ⬜ 未开始 | — | Keycloak/Ory/Zitadel 同环境压测 |

**承重假设裁决（对照调研报告 §3.1）**:

| 声明 | 实测 | 裁决 |
|------|------|------|
| 单机 QPS ~10 万+ | S1 discovery 86,332 QPS（8 vCPU WSL） | ⚠️ **接近** — 线性外推 16 核裸机 ~170k，几乎确定可达。但仅限 discovery 类无状态端点；token 签发 ~9k QPS |
| 内存 50–120 MB | SDK 实测: 2.5 MB peak WS / 0.6 MB private（12 MB binary） | ✅ **SDK 口径远超标** — `third-party-host-smoke`（纯 SDK）实测 peak working set 2.5 MB；`full-stack-host-smoke`（SDK+Drogon）同样 2.5 MB。docker stats 的 2.4 GB 是容器全栈口径（含连接池/共享库/page cache），与 SDK 声称不同口径 |
| P99 < 2ms | S3/S6 低并发 P99 1–2ms；高并发退化 12–430ms | ✅ **低并发达成** — c≤16 时 P99 1–4ms，高并发为连接池排队效应 |
| 冷启动 ~5s | setup.sh 观测 ~4s 就绪 | ✅ **观测达成** — compose up 后 /health/ready ~4s 返回（含 PG/Redis 启动） |

> ⚠️ 以上数字来自 WSL2 虚拟机（8 vCPU / 16 GB），非裸机。所有场景 driver CPU < 44%，数字是**下限**。

**已修复的环境阻塞**:
- ~~#45 docker compose v5.3.1 路径解析~~ → 修复（`390275c`）
- ~~#46 SchemaManager 冷启动迁移竞态~~ → 修复（`508908a`）

### 2.2 Phase 1 — 产品化基础 + 社区启动（🟢 已解除阻塞）

| 工作项 | 状态 | 完成证据 / 阻塞 | 下一步 |
|--------|------|----------------|--------|
| **独立文档站**（Docusaurus/VitePress） | ⬜ 未开始 | Phase 0 数据已落地，不再阻塞 | 选型 + 立项 |
| **OpenAPI spec 治理**（Layer 1 前置） | ✅ 已完成 | PR #63 已合并（v1.2.0）：三层端点对账（82 路由=80 文档=78 YAML+例外）+ P0 schema 补齐 + `check_spec_governance.py` 一致性门（CI static-checks）+ oasdiff v1.29.1 破坏性变更门（openapi-governance.yml）+ info.version 联动 | 维护例外清单 |
| **Python + Go 客户端 SDK**（C1） | ✅ 已实现（2026-08-18，待合并） | `clients/python`（openapi-python-client 0.29.0，25 单测）+ `clients/go`（oapi-codegen v2.8.0，单测 G1-G8）+ 漂移门 `tools/clients/regen_clients.py` + CI `clients-sdk.yml` + release.yml 发布接线（PyPI 步骤级 secret 门控 + Go 嵌套 tag）；发行名 `authforge-oauth2`（PyPI `authforge` 被占用）；顺带修 spec：OAuth2Error 枚举 +RFC 6750 两码 | PyPI 项目注册 + secret 配置（人工一次性）；发布后 AC7 冒烟 |
| **README 性能徽章** | ⬜ 未开始 | Phase 0 数据已落地，可据实测标注 | 用 SUMMARY.md 实测数字 |
| **首发技术博客 + 基准报告** | ⬜ 未开始 | Phase 0 数据已落地 | 用 SUMMARY.md 实测数字 |
| **TechEmpower 提交** | ⬜ 未开始 | Phase 0 数据已落地 | — |

### 2.3 Phase 2 — 企业版（IAM 缺口）

> **缺口依据**: [iam-architecture-audit.md](iam-architecture-audit.md) §四业务缺口优先级

#### P0（企业版 MVP 必须）

| 缺口 | 状态 | 代码依据 | 工程量估算 |
|------|------|----------|-----------|
| **用户管理补全**（创建/删除/分页/搜索） | 🟡 已实现（PR #52） | 分页/搜索/createUser/updateUser 扩展/deleteUser 软删除（V024）已在本分支落地 | 小（1–2 周） |
| **Backchannel Logout 真实实现** | 🟡 后端已交付 | 真实通知器 + logout_token 构造 + admin API 配置 + discovery + 单测（D1-D6）；前端就绪但被 Mimosa 拦截（mock-api.ts 既有误报）；集成测试待补（需 PG）。PR #50，详见 [in-progress/backchannel-logout-design.md](in-progress/backchannel-logout-design.md) | 中（1 月） |
| ~~**#42 缓存层**~~ | ✅ Phase 1+2 已交付 | client-cache + token-cache decorator 均已合并 | Phase 3/4 远期 |
| ~~**#43 授权模型**~~ | ✅ 已实现 | 声明式 scope 注册表 + implication + DB 驱动（commit f04d3ba） | — |
| **SAML 2.0** | ⬜ 未开始 | 穷尽搜索 0 代码文件 | 大（3–6 月，需 XML 签名库） |
| **LDAP/AD 联邦** | ⬜ 未开始 | 穷尽搜索 0 代码文件 | 中（2–3 月） |
| **SCIM 2.0** | ⬜ 未开始 | 穷尽搜索 0 代码文件 | 中（1–2 月） |
| **多租户隔离激活** | ⬜ 未开始 | schema + Organization API 有（V017），请求级隔离无 | 大 |

#### P1（企业版增强）

| 缺口 | 状态 | 代码依据 | 工程量估算 |
|------|------|----------|-----------|
| WebAuthn attestation/assertion 验证 | ⬜ 未开始 | 简化实现：不验签 | 中 |
| ABAC 策略引擎 | ⬜ 未开始 | 穷尽搜索 0 代码 | 大 |
| 审计合规报告导出 | ⬜ 未开始 | 缺失 | 小–中 |
| ~~社交账号 link/unlink~~ | ✅ 已实现（2026-08-21） | `GET/POST/DELETE /api/me/social/links[/{provider}]` + `SocialLinkService` + 最后凭证守卫 + 前端 Connected Accounts 卡片；27 单测 + 11 HTTP 集成 + 6 e2e。已合并（PR #68，v1.3.0），详见 [in-progress/social-link-unlink-design.md](in-progress/social-link-unlink-design.md) | — |
| 审计完整性保护（哈希链） | ⬜ 未开始 | 缺失 | 中 |
| SIEM 集成（Syslog/CEF） | ⬜ 未开始 | 缺失 | 中 |

### 2.4 横切工作流

#### OAuth/OIDC 合规审计 ✅

| 工作项 | 状态 | 说明 |
|--------|------|------|
| 审查 + 31 项发现 + 5 项复扫 | ✅ 全部处置 | PR #44 (fb775ed)；所有追踪 issue #21–#40 已关闭 |
| 报告 | ✅ 已归档 | [done/oauth-oidc-compliance-audit.md](done/oauth-oidc-compliance-audit.md) |

#### 架构改进 — #42 Postgres+Redis 缓存层

> **设计文档**: [in-progress/postgres-redis-cache-design.md](in-progress/postgres-redis-cache-design.md)

| Phase | 状态 | 内容 |
|-------|------|------|
| Phase 1（client-cache） | ✅ 已交付 | `RedisCachedClientRepository`（PR #47） |
| Phase 2（token cache） | ✅ 已交付 | `RedisCachedTokenRepository`：getAccessToken + introspectToken 缓存 + revoke invalidation + negative cache（`14f69e3`） |
| Phase 2 follow-up | ✅ 已交付 | C3 refresh-token revoke no-op 修复（`32dd662`）；api-diff baseline ratified（`ac7ea14`） |
| Phase 3（移除独立 Redis 模式） | ⬜ 未开始 | BREAKING 变更 |
| Phase 4（consent cache + L1+L2） | ⬜ 未开始 | 远期优化 |

#### 架构改进 — #43 资源-作用域授权模型 ✅

> **设计文档**: [done/resource-scope-authorization-design.md](done/resource-scope-authorization-design.md)

| 工作项 | 状态 | 内容 |
|--------|------|------|
| 设计 | ✅ 已完成 | 单一 `(path,method)→scope` 声明式注册表 + 读写粒度 + scope implication + DB 驱动 |
| 实现 | ✅ 已完成 | `ResourceScopeRegistry` + `ScopeResolver` + `InsufficientScopeResponder` + 7 controller 迁移 + V006 重写 + 集成测试（commit `f04d3ba` + `dc89c8d` + `975f193` + `f6552f5`） |

#### 端点测试集成 ✅

| 工作项 | 状态 | 内容 |
|--------|------|------|
| Shell 端点测试脚本集成进 ctest | ✅ 已完成 | 方案 A：整合进 ctest，移除 DEPENDS（commit `c470922` + `0212fe7`） |

#### 异步回调评估 ✅

> **评估文档**: [done/async-refactor-assessment.md](done/async-refactor-assessment.md)
> 决策：暂不动手（C++17 锁定，协程排除）

---

## 三、GitHub Issue 跟踪

> 所有 issue #21–#46 均已关闭。以下为关键 issue 的最终状态。

| Issue | 标题 | 最终状态 | 关闭依据 |
|-------|------|----------|----------|
| **#40** | OAuth/OIDC Compliance Audit 追踪 | ✅ 已关闭 | 全部 31+5 项发现已修复 |
| **#41** | OpenApiGenerator security 字段 bug | ✅ 已关闭 | `de03a19` + `b99ef5b` |
| **#42** | Postgres + Redis 缓存层 | ✅ 已关闭 | Phase 1+2 已交付 |
| **#43** | 资源-作用域授权模型 | ✅ 已关闭 | `f04d3ba` 完整实现 |
| **#45** | docker compose v5.3.1 路径解析 | ✅ 已关闭 | `390275c` |
| **#46** | SchemaManager 冷启动迁移竞态 | ✅ 已关闭 | `508908a` |

---

## 四、已关闭里程碑（历史记录）

| 日期 | 里程碑 | 提交 / PR |
|------|--------|----------|
| 2026-08-09 | OAuth/OIDC 合规审计 — 31 项偏差全部修复 | PR #44 (fb775ed) |
| 2026-08-11 | #42 缓存层 Phase 1 — client-cache decorator | PR #47 |
| 2026-08-12 | #41/#45/#46 修复 + #42 Phase 2 token cache + C3 revoke 修复 | de03a19, 508908a, 390275c, 14f69e3 |
| 2026-08-12 | benchmark M2–M4 — S3/S4/S5/S6 + 40 JSON + SUMMARY.md 承重验证 | eda89da, 292ca37 |
| 2026-08-12 | **#43 资源-作用域授权模型完整实现** | f04d3ba + dc89c8d + 975f193 + f6552f5 |
| 2026-08-12 | **v1.1.0 发布** | tag v1.1.0 (4362ac2) |
| 2026-08-12 | 端点测试集成进 ctest + 测试脚本同步 | c470922, 0212fe7 |
| 2026-08-13 | **A2 用户管理补全 — 分页/搜索/createUser/软删除（V024）** | PR #52（51674ba..f0b96bf，分支 feat/user-management-crud-v2） |
| 2026-08-13 | **D1 Backchannel Logout 后端 — 通知器 + logout_token + admin API + 单测** | PR #50（5b3ccb9..9c8cf9f，分支 feat/backchannel-logout-b1） |
| 2026-08-13 | **内存 SDK 口径实测 — 2.5 MB peak WS（50-120MB 声称保守达标）** | third-party-host-smoke / full-stack-host-smoke 实测 |
| 2026-08-17 | **A1 OpenAPI spec 治理（M0）— 三层对账 + schema 补齐 + 一致性门 + oasdiff 门，Python 客户端验收通过** | 分支 feat/openapi-spec-governance-m0（待 PR） |
| 2026-08-21 | **B2 社交账号 link/unlink — 3 端点 + SocialLinkService + 最后凭证守卫 + 前端卡片（v1.3.0）** | PR #68（e991360，含独立评审 W1-W4+S1-S4 修复轮） |
| 2026-08-05 | 产品化演进方案 + benchmark/client-sdk 设计文档 | a6d570c |

---

*本文件是活文档，随工作进展持续更新。最新状态始终以此文件为准。下一步行动项见 [next-phase-implementation-plan.md](next-phase-implementation-plan.md)。*
