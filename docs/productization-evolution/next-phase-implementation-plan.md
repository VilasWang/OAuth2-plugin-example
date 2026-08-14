# 下一阶段实施计划

> **更新日期**: 2026-08-13
> **上游规划**: [productization-evolution-plan.md](productization-evolution-plan.md)（总体路线图）
> **IAM 缺口依据**: [iam-architecture-audit.md](iam-architecture-audit.md) §四
> **当前状态**: [progress-status.md](progress-status.md)
> **性质**: 近可执行实施计划（2–4 个月窗口），非长期路线图

---

## 一、上一阶段完成回顾

本计划的前一版本（2026-08-11）包含 4 个梯队。以下工作项已**全部完成**，从待做列表移除：

| 原任务 | 完成时间 | 产出 |
|--------|----------|------|
| ~~A2 关闭 #40~~ | 2026-08-09 | PR #44 合并，所有追踪 issue 关闭 |
| ~~A3 修 #41 spec bug~~ | 2026-08-12 | `de03a19` + `b99ef5b` |
| ~~B1 #43 授权模型~~ | 2026-08-12 | `f04d3ba` — 完整声明式 scope 注册表 |
| ~~B2 #42 token cache Phase 2~~ | 2026-08-12 | `14f69e3` — RedisCachedTokenRepository |
| ~~C1 benchmark M2–M3~~ | 2026-08-12 | `eda89da` — S3/S4/S5/S6 场景 |
| ~~C2 benchmark M4 承重报告~~ | 2026-08-12 | `292ca37` — 40 JSON + SUMMARY.md |

**关键里程碑**: v1.1.0 已发布（tag `v1.1.0`），包含 OAuth/OIDC 合规修复 + #42/#43 架构改进 + benchmark 设施。

---

## 二、当前状态与优先级重定

### 2.1 benchmark 承重假设裁决的影响

benchmark M4 报告（`benchmarks/results/SUMMARY.md`）产出了实测数据，对后续策略有直接影响：

| 声明 | 实测裁决 | 对 Phase 1 的影响 |
|------|----------|-------------------|
| QPS ~10 万+ | ⚠️ 接近（discovery 86k on 8 vCPU，可外推达标） | **可用于对外传播**，但须限定"无状态端点"场景 |
| P99 < 2ms | ✅ 低并发达成（c≤16 时 1–4ms） | **可用于对外传播**，须限定并发范围 |
| 内存 50–120 MB | ⚠️ 口径不匹配（docker stats 容器 RSS 2.4 GB vs SDK 逻辑层声称） | **不可使用 docker stats 数字否定原声称**；须用 SDK 嵌入口径（`examples/third-party-host/` PSS）重新测量 |
| 冷启动 ~5s | ✅ 观测达成（setup.sh 观测 ~4s 就绪） | **可用于对外传播** |

**结论**: Phase 1 的对外传播（博客/README 徽章/TechEmpower）**现已解除阻塞**，但须诚实标注场景限定。research.md §3.1 的性能数字需要用实测值替换或标注。

### 2.2 仍待解决的高优先级工作

基于 IAM 审计缺口 + benchmark 裁决，剩余优先级排序：

1. **Phase 1 启动准备**（spec 治理 → 客户端 SDK → 文档站 → 博客）——现在可以启动
2. **IAM P0 缺口**（用户管理补全 + Backchannel Logout）——性价比最高
3. **IAM P1 缺口**（社交 link/unlink + 审计合规导出）——增强项

---

## 三、下一阶段工作项（按优先级排序）

### 第一梯队：Phase 1 启动 + IAM 低垂果实（立即启动，2–4 周）

#### A1. OpenAPI spec 治理（Phase 1 Layer 1 前置）★ 解除全部客户端工作阻塞

> **设计文档**: [todo/client-sdk-facility-design.md](todo/client-sdk-facility-design.md)
> **前置**: ~~#41~~ 已修复；死孤儿 openapi.json 已删
> **工程量**: 1–2 周

**现状**: `apps/server/openapi.yaml` 路径覆盖足（69 操作）但 requestBody/response schema 严重稀疏（D1.5），无法直接驱动客户端生成。

**实施步骤**:
1. 补齐 YAML 的 requestBody / response schema / schema 定义（M0 核心工作量）
2. 新增 YAML↔代码一致性门（关闭"C++ 改端点忘改 YAML"的治理漏洞）
3. 引入 oasdiff 破坏性变更门（客户端可发布的前提）
4. 验收：生成的 Python 客户端能成功调用 `/oauth2/token` + `/oauth2/introspect`

#### A2. 用户管理补全 ★ 最高性价比 IAM 缺口

> **IAM 审计**: §四 P0
> **工程量**: 1–2 周 | **验证**: 2026-08-11 确认仍缺失

**现状（验证 2026-08-11）**:
- `UserAdminService.cc:82-84` — listUsers 全量查询，无分页
- 无 createUser / deleteUser / 搜索过滤
- updateUser 仅 2 字段（email + email_verified）
- `UserAdminController.cc:26` OpenAPI 描述谎称 "paginated"

**实施步骤**:
1. 分页：`listUsers` 加 `page`/`per_page` 参数 + `mapper.paginate()`（复用 AuditService 模式）
2. 搜索：加 `q`（用户名/邮箱模糊）、`role`、`locked` 参数
3. 创建：新增 `POST /api/admin/users`（密码 PBKDF2 哈希 + 默认角色 + 审计日志）
4. 删除：新增 `DELETE /api/admin/users/{userId}`（软删除 `deleted_at` + 级联吊销 token）
5. updateUser 扩展：增加 `username`、`mfa_enabled`、`locked`、`org_id`
6. 修正 OpenAPI 描述
7. 集成测试覆盖

#### A3. 首发技术博客 + README 性能标注 ★ 对外传播启动

> **前置**: Phase 0 数据已落地（SUMMARY.md）
> **工程量**: 3–5 天

**实施步骤**:
1. README 性能徽章 + "如何复现"小节（用 SUMMARY.md 实测数字，诚实标注场景限定）
2. 首发博客草稿：「为什么我们用 C++ 构建 OAuth2 服务器」+ 实测对比
3. research.md §3.1 修订：用实测值替换工程估算

---

### 第二梯队：IAM P0 缺口推进（4–8 周，可与第一梯队并行）

#### B1. Backchannel Logout 真实实现

> **IAM 审计**: §四 P0 | **工程量**: 中（1 月）

**现状**: `IdentityAssembly.cc:49-57` LoggingBackchannelLogoutNotifier 仅 LOG_DEBUG。DB 层已有 `backchannel_logout_uri`/`backchannel_logout_session_required` 字段（V015）。

**实施步骤**:
1. 查询 client 的 backchannel_logout_uri
2. 构造 logout_token JWT（OIDC Back-Channel Logout 1.0: sub/aud/iat/jti/events）
3. HTTP POST 到每个 RP 的 backchannel_logout_uri（超时/重试）
4. 集成测试

#### B2. 社交账号 link/unlink

> **IAM 审计**: §四 P1 | **工程量**: 小（3–5 天）

**现状**: `ISocialAccountRepository` 接口存在，仅被 GitHubAuthService find-or-create 消费，未暴露为 REST 端点。

**实施步骤**:
1. `POST /api/me/social/links/{provider}`（发起 OAuth 关联流）
2. `DELETE /api/me/social/links/{provider}`（解除关联）
3. UserSelfServiceController 路由 + service 方法
4. 集成测试

---

### 第三梯队：Phase 1 核心交付（第一梯队完成后启动，6–12 周）

#### C1. Python + Go 客户端 SDK

> **设计文档**: [todo/client-sdk-facility-design.md](todo/client-sdk-facility-design.md)
> **前置**: A1 spec 治理完成

**实施步骤**:
1. M1: openapi-python-client 生成 + 手写 auth 层（httpx transport）
2. M2: oapi-codegen 生成 Go 客户端 + 手写 auth 层（golang.org/x/oauth2/clientcredentials）
3. M3: 发布到 PyPI / Go module proxy
4. M4: 端到端文档 + 基准复现示例

#### C2. 独立文档站（Docusaurus）

> **工程量**: 2–3 周

**实施步骤**:
1. 选型 Docusaurus（中英双语 + 版本化）
2. 搬运 docs/backend/* 内容上站
3. benchmark 数据页（引用 SUMMARY.md）
4. 与 release 联动：每 tag 一个版本快照

#### C3. benchmark 补完 + 竞品对比（Phase 0.5）

> **工程量**: 2–3 周

**实施步骤**:
1. 冷启动精确测量（`measure-cold-start.sh`）
2. 内存重新测量（区分 OAuth2 逻辑层 vs 全栈 RSS）
3. Keycloak / Ory / Zitadel 同环境压测
4. 更新 SUMMARY.md + research.md

---

## 四、明确排除（本期不启动）

| 排除项 | 理由 |
|--------|------|
| **SAML 2.0 / LDAP / SCIM** | 工程量大（6–11 月），需客户驱动的最小子集先行 |
| **云托管（Phase 3）** | 未达启动门槛 |
| **ABAC / 风控引擎** | P2 级，远期 |
| **异步回调重构** | 评估结论为"暂缓"（C++17 锁定，协程排除） |
| **WebAuthn attestation 验证** | 简化实现在安全上可接受（浏览器已验证），仅高合规场景需要 |

---

## 五、依赖关系图

```
A1 (spec 治理) ──────────────→ C1 (Python/Go SDK) ──→ C2 (文档站)
                                                    ↗
A3 (博客 + README 标注) ←─ Phase 0 数据（已就绪）
                                                    ↗
A2 (用户管理) ←── 无依赖，可立即开始
B1 (Backchannel Logout) ←── 无依赖，可立即开始
B2 (社交 link/unlink) ←── 无依赖，可立即开始

C3 (benchmark 补完) ←── 独立，可与任何任务并行
```

**关键路径**: A1 spec 治理 → C1 客户端 SDK（Phase 1 的阻塞链）

---

## 六、并行安全分析

| 任务对 | 文件交叉 | 安全？ |
|--------|----------|--------|
| A1 ↔ A2 | openapi.yaml（文档，可接受）+ 不同 service/controller | ✅ 安全 |
| A1 ↔ B1 | 无 | ✅ 安全 |
| A2 ↔ B1 | 无（UserAdminService vs IdentityAssembly） | ✅ 安全 |
| A2 ↔ B2 | 无（UserAdminController vs UserSelfServiceController） | ✅ 安全 |
| B1 ↔ B2 | IdentityAssembly.cc 不同段（低风险） | ⚠️ 可并行 |
| A3 ↔ 任意 | 仅改文档/README | ✅ 安全 |
| C3 ↔ 任意 | 仅改 benchmarks/ 脚本 | ✅ 安全 |

**推荐并行分组**: `A2(用户管理) + B1(Backchannel Logout) + C3(benchmark补完)` 三路并行；A1(spec 治理) 单独一条线（关键路径）。

---

## 七、风险与缓解

| 风险 | 概率 | 影响 | 缓解 |
|------|------|------|------|
| **内存数字口径争议** | 中 | 低（口径问题，非能力问题） | docker stats 测容器全栈 2.4GB（口径不匹配）；改用 SDK 嵌入 PSS 口径（`examples/third-party-host/`）重新测量 |
| **spec 治理工作量超预期** | 中 | 高（阻塞 Phase 1 全线） | YAML 内容稀疏（D1.5），M0 工作量大于路径覆盖暗示的规模 |
| **SAML/LDAP/SCIM 客户需求突然出现** | 低 | 高 | 保持设计前置评估能力；本期明确排除 |
| **竞品对比证伪性能优势** | 中 | 高 | Phase 0.5 对比前不发布竞品对比声明；先自测，再竞品 |

---

## 八、决策待确认项

| # | 决策 | 选项 | 建议 |
|---|------|------|------|
| 1 | A2 用户删除策略 | 软删除（`deleted_at`）vs 硬删除 | **软删除**（GDPR + 可恢复） |
| 2 | A3 README 性能标注口径 | 全栈数字 vs 限定场景 | **限定场景**（"discovery 86k QPS / token 签发 9k QPS"而非笼统"10万+"） |
| 3 | 内存卖点口径 | 全栈 RSS vs OAuth2 逻辑层 | 待 Phase 0.5 精确测量后定 |
| 4 | 客户端 SDK 语言优先级 | Python 先 vs Go 先 vs 同时 | **Python 先**（受众更广），Go 紧随 |

---

*本计划是 2–4 个月窗口的近可执行实施计划。长期路线图见 [productization-evolution-plan.md](productization-evolution-plan.md)。当前状态见 [progress-status.md](progress-status.md)。*
