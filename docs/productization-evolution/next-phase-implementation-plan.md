# 下一阶段实施计划

> **更新日期**: 2026-08-24（第 4 次修订：v1.4.0 已发布、五场景竞品对比全领先、A1/A2/A3徽章/B1/B2/C1/C3 全部交付；重心转向安全修复 + 对外传播 + 文档站）
> **上游规划**: [productization-evolution-plan.md](productization-evolution-plan.md)（总体路线图）
> **IAM 缺口依据**: [iam-architecture-audit.md](iam-architecture-audit.md) §四
> **当前状态**: [progress-status.md](progress-status.md)
> **性质**: 近可执行实施计划（2–4 个月窗口），非长期路线图

---

## 一、上一阶段完成回顾

截至 2026-08-24，原计划的全部三个梯队已**基本清零**：

| 原任务 | 完成时间 | 产出 |
|--------|----------|------|
| ~~A1 spec 治理~~ | 2026-08-17（v1.2.0） | PR #63：三层对账 + schema 补齐 + 一致性门 + oasdiff 门 |
| ~~A2 用户管理补全~~ | 2026-08-13 | PR #52：分页/搜索/createUser/软删除（V024）+ #53/#56/#58-#60 评审加固 |
| ~~A3 README 性能标注~~ | 2026-08-24（v1.4.0） | 双语徽章 + Performance 小节（五场景表）+ 如何复现 |
| ~~B1 Backchannel Logout~~ | 2026-08-22 全栈 | PR #61：通知器 + logout_token + admin API/UI + #55/#57 加固 |
| ~~B2 社交账号 link/unlink~~ | 2026-08-21（v1.3.0） | PR #68：3 端点 + SocialLinkService + 最后凭证守卫 + 前端卡片 |
| ~~C1 Python/Go 客户端 SDK~~ | 2026-08-18 起 | PR #65：**PyPI `fulla-oauth2` 1.4.0 live** + Go 嵌套 tag + 漂移门 + CI |
| ~~C3 竞品对比（Phase 0.5）~~ | 2026-08-22 + 08-21/23 两轮刷新 | PR #64：Keycloak/Ory/Zitadel 套件 + **五场景全领先**（COMPARISON.md） |
| ~~性能优化两轮~~ | 2026-08-19 ~ 08-24 | wave-1（PG17/LTO/池调优）+ wave-2（代码级缓存）+ TTL=30 留存收官 |

**关键里程碑**: v1.2.0（spec 治理）→ v1.3.0（社交 + SDK）→ **v1.4.0（PG17 + 性能徽章 + PyPI live，全工作流绿）**。

---

## 二、当前状态与优先级重定

### 2.1 形势变化

1. **对外传播的证据链已完整**：五场景同环境对比全领先（COMPARISON.md 2026-08-23）+ SDK 内存 2.5 MB + 冷启动 1.26s 四家最快 + PyPI 已 live——博客的所有素材就绪。
2. **但上线功能暴露了新缺口**：社交登录（PR #68）、软删除（PR #52）、缓存（PR #47/#61）等已上线功能产生了 **3 个 High 安全 issue（#54/#78/#79）+ 1 个用户可见功能 bug（#69）**。对外传播之前应先堵安全洞——"先修门锁，再开门迎客"。
3. **GC/尾延迟叙事已诚实关闭**：本机四家同款噪声，尾延迟主张待裸机复测。博客表述须遵循 research.md §3.2 item 2 的口径约束。

### 2.2 剩余优先级排序

1. **安全批次**（#78 end_session 验签、#79 缓存回填竞态、#80 DEL 失败告警、#54 软删除社交绕过）——最高优先
2. **社交登录修复批次**（#69 401 bug、#71 状态校验、#70 subject mappings、#73 守卫竞态）——用户可见缺陷
3. **对外传播启动**（A3 剩余：首发博客）——素材全就绪
4. **文档站（C2）**——Phase 1 最后一块大拼图
5. **卫生批次**（#81 CI PG17、#72 测试可重跑、#82/#83/#84/#66 小项）——穿插处理

---

## 三、下一阶段工作项（按优先级排序）

### 第一梯队：安全修复（立即启动，1–2 周）

#### S1. end_session id_token_hint 验签（#78）★ 安全最高

> **Issue**: #78 [High] | **工程量**: 2–4 天

**现状**: `/oauth2/end_session` 直接解析未验证的 `id_token_hint` → 任何知道目标用户标识的攻击者可强制登出他人（跨用户强制登出）。OIDC RP-Initiated Logout §3 要求 id_token_hint **必须验签**。

**实施步骤**:
1. id_token_hint 经 JWKS 验签（复用 `JwkManager` + TokenService 的 JWT 验证路径）
2. 验签失败/过期/iss 不符 → 拒绝（400 `invalid_id_token_hint`），不得回退到"当作未提供"
3. 验证 sub 与当前会话一致才执行登出
4. 回归：RP-Initiated Logout 既有测试 + 新增"伪造 hint 被拒"负例

#### S2. Redis cache-aside 回填竞态加固（#79 + #80）

> **Issue**: #79 [High] + #80 [Medium] | **工程量**: 3–5 天

**现状**: 读 miss → 回填期间并发写（secret 轮换/角色撤销）可能被旧值回填覆盖，固化已轮换 secret/已撤销角色直至 TTL；失效 DEL 失败仅 LOG_DEBUG。

**实施步骤**:
1. 写路径失效改为"先删后写 + 短双删"或版本号（世代标记），确保回填不会覆盖更新
2. DEL 失败升级为 LOG_WARN + 计数器（IMetrics），可选一次重试
3. 集成测试：并发"读 miss 回填 vs secret 轮换"竞态场景
4. 复查 PR #47 评审时指出的同类模式（admin-mutation invalidation）

#### S3. 软删除 × 社交登录绕过（#54）

> **Issue**: #54 [High] | **工程量**: 1–2 天（与 F1 同分支做）

**现状**: deleteUser 软删除后，GitHub 社交登录 find-or-create 路径不检查 `deleted_at` → 软删除用户可重新登录拿新 token。

**实施步骤**:
1. 社交 find-or-create 查用户时过滤/检查 `deleted_at IS NULL`
2. 命中软删除用户 → 拒绝登录（ACCOUNT_DISABLED 语义）
3. 集成测试：软删除 → 社交登录 → 必须 401

---

### 第二梯队：社交登录修复批次（1–2 周，可与第一梯队部分并行）

#### F1. GitHub 社交 token 存储/校验口径统一（#69）★ 用户可见 bug

> **Issue**: #69 | **工程量**: 1–2 天

**现状**: PR #68 后社交签发的 token **原文存储**但后续按**哈希**校验 → GitHub 社交会话**每次请求 401**。

**实施步骤**: 统一为哈希存储+哈希校验（与密码凭证同口径）；存量数据迁移（原文 → 哈希，或直接失效令重登）；集成测试覆盖"社交登录后调 /api/me 必须成功"。

#### F2. link 流程服务端状态校验（#71）+ subject mappings 消费（#70）+ 守卫加固（#73）

> **Issues**: #71（provider-code 注入/login-CSRF）+ #70（Google/WeChat 不消费 mappings）+ #73（并发解绑竞态 + WebAuthn 计入最后凭证守卫） | **工程量**: 3–5 天

**实施步骤**:
1. #71: link 请求生成一次性 state（Redis/DB 短 TTL），回调校验 state + 绑定发起者会话
2. #70: Google/WeChat 登录路径接入 `ISubjectMappingRepository`（对齐 GitHub 行为）
3. #73: 解绑守卫原子化（SELECT FOR UPDATE 或乐观锁）；WebAuthn 凭证计入"最后凭证"判断
4. 各配集成测试

---

### 第三梯队：对外传播 + 文档站（素材已全就绪）

#### M1. 首发技术博客（A3 剩余）

> **工程量**: 3–5 天 | **前置**: 建议第一/二梯队安全修复合并后发布

**素材**（全部就绪）: COMPARISON.md 五场景对比（S1 2.1x / S2 2.6x / S3 2.0x / S5 1.9x / S6 1.5x）+ 冷启动 1.26s vs Keycloak 18.3s + SDK 2.5 MB + PyPI `pip install fulla-oauth2` + Go module。

**内容骨架**: 「为什么我们用 C++ 构建 OAuth2 服务器」——同环境四产品对比方法论（可复现脚本）→ 结果与诚实限定（GC 抖动叙事关闭的坦白是加分项）→ 嵌入式 SDK 差异化。
**渠道**: HN / Reddit r/cpp / r/netsec（evolution-plan §三 Phase 1 既定）。
**红线**: 遵循 research.md §3.2 口径——尾延迟不提"零 GC 抖动"；内存区分 SDK/容器口径；竞品数字只引 COMPARISON.md。

#### M2. 独立文档站（C2，Docusaurus）

> **工程量**: 2–3 周 | **前置**: 无硬前置，可与博客并行

**实施步骤**:
1. Docusaurus 骨架（中英双语 + 版本化，与 release tag 联动）
2. 内容搬运：SDK 集成指南/运行时契约/API 参考/架构概览 + benchmark 数据页（引 COMPARISON.md）+ 客户端 SDK 快速上手（PyPI/Go）
3. 部署 GitHub Pages；release.yml 加站点构建 job
4. TechEmpower 提交评估（可选，依赖裸机基准——本机数据不满足其硬件要求）

---

### 第四梯队：卫生批次（穿插处理，各 ≤1 天）

| Issue | 内容 | 备注 |
|-------|------|------|
| **#81** | CI service 容器 postgres:15 → 17 | 已有升级 runbook（074c4d8f），照做即可 |
| **#72** | SoftDeleteSocialRepoTest + UserAdminHardeningTest 不可重跑 | 测试隔离（fixture 清理顺序） |
| **#82** | Backchannel notifier user_id 双形态匹配 | 对齐 cbb15e40 的双形态处理 |
| **#83** | `ensure_audit_partitions()` 调度器 | V025 配套，24 个月后才实际触发，低急迫 |
| **#84** | 文档漂移（PG15 残留等） | 6acade8d 已清部分，扫尾 |
| **#66** | openapi.yaml client-credentials 描述与 F-017 Basic 实现不符 | spec 文档修正 |
| — | **分支清理**：`feat/competitor-benchmark` 本地 ref 13 个 commit 疑为 rebase 合并前的原始 SHA（内容已在 master，PR #64），核对 patch-id 后删除本地/远端分支 | 防止误判"有未合并工作" |

---

## 四、明确排除（本期不启动）

| 排除项 | 理由 |
|--------|------|
| **SAML 2.0 / LDAP / SCIM** | 工程量大（6–11 月），需客户驱动的最小子集先行 |
| **多租户隔离激活** | 大工程，无近期客户信号 |
| **云托管（Phase 3）** | 未达启动门槛（自托管付费客户 ≥ N） |
| **ABAC / 风控引擎** | P2 级，远期 |
| **异步回调重构** | 评估结论"暂缓"（C++17 锁定） |
| **裸机基准复测**（尾延迟/GC 归因） | 无裸机基准机；待有硬件时再做，当前叙事已按诚实口径收敛 |
| **TechEmpower 提交** | 依赖裸机基准，暂缓评估 |

---

## 五、依赖与并行分析

```
S1 (#78 end_session 验签)      ←─ TokenService/JwkManager 区域
S2 (#79/#80 缓存竞态)          ←─ storage-redis 区域          S1 ∥ S2 安全
S3 (#54 软删除社交绕过)        ←─ 社交 find-or-create ─┐
F1 (#69 token 口径)            ←─ 社交 token 签发/校验 ─┴─ 同区域，建议同分支串行
F2 (#71/#70/#73)               ←─ SocialLinkService        F1/S3 完成后做

M1 博客                        ←─ 文档，零代码依赖，随时可写（发布等安全批合并）
M2 文档站                      ←─ 新目录 docs/site/，与一切并行安全
第四梯队卫生项                 ←─ 各自独立，可穿插
```

**推荐节奏**: 第 1–2 周 `S1 ∥ S2 ∥ (S3+F1+F2 串行同分支)` + 卫生项穿插 → 第 3 周合并后发博客（M1）∥ 文档站启动（M2）。

---

## 六、风险与缓解

| 风险 | 概率 | 影响 | 缓解 |
|------|------|------|------|
| **#78/#79 是 High 安全洞，对外传播放大曝光面** | 中 | 高 | 博客发布前必须合并 S1/S2；发布顺序=先修门锁后迎客 |
| **社交批次修复引发 PR #68 回归** | 中 | 中 | 全量后端测试 + 社交 27 单测/11 HTTP 集成/6 e2e 回归门 |
| **竞品对比被质疑方法论** | 中 | 中 | COMPARISON.md 已含完整方法论 + 复现脚本 + 诚实注记（Keycloak S6 重跑修复）；博客附复现指引 |
| **博客发布后 issue 洪峰** | 低 | 中 | #69/#54 等用户可见 bug 先修；文档站（M2）承接流量 |
| **裸机数据缺席导致尾延迟叙事受限** | 高 | 低 | 已诚实关闭该主张；卖点集中在五场景 QPS/冷启动/SDK 内存 |

---

## 七、决策待确认项

| # | 决策 | 选项 | 建议 |
|---|------|------|------|
| 1 | 博客发布时机 | 安全批合并后立即 vs 等文档站一起 | **安全批合并后立即**（M2 承接流量，不必互相等） |
| 2 | 博客首发语言/渠道 | 英文 HN/Reddit 先 vs 中英双发 | **英文先**（受众主战场），中文社区转载随后 |
| 3 | #79 竞态修复方案 | 双删 + TTL 缩短 vs 世代标记（versioning） | **先双删（简单）**，世代标记作为 #42 Phase 3 重构时一并考虑 |
| 4 | #83 审计分区调度器 | pg_cron vs 应用内定时 | **应用内定时**（复用 OAuth2CleanupService 模式，不引入 PG 扩展依赖） |

---

*本计划是 2–4 个月窗口的近可执行实施计划。长期路线图见 [productization-evolution-plan.md](productization-evolution-plan.md)。当前状态见 [progress-status.md](progress-status.md)。*
