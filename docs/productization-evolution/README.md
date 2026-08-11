# 产品化演进 · Productization Evolution

本目录承载 AuthForge 从开源项目向商业产品演进的规划、设计与进展记录。

## 目录结构（按状态分类）

```
productization-evolution/
├── README.md                           ← 本文件（目录索引 + 生命周期约定）
├── productization-evolution-plan.md    ← 总体演进方案（路线图锚点）
├── productization-research.md          ← 产品化调研报告（市场/竞品/路线输入）
├── iam-architecture-audit.md           ← IAM 业务架构调研报告（代码验证）
├── progress-status.md                  ← 进展总览（全 phase 的工作项状态）
├── next-phase-implementation-plan.md   ← 下一阶段详细实施计划
│
├── done/                               ← 已完成任务的方案/计划/审计文档
│   ├── oauth-oidc-compliance-audit.md          (31 项偏差全部修复, PR #44)
│   ├── oauth-oidc-compliance-audit-plan.md     (审查计划)
│   └── async-refactor-assessment.md            (异步回调评估报告)
│
├── in-progress/                        ← 进行中任务的方案/设计文档
│   ├── benchmark-facility-design.md            (Phase 0, M1 已交付, M2–M4 待做)
│   ├── postgres-redis-cache-design.md          (#42 Phase 1 已交付, Phase 2 设计)
│   └── resource-scope-authorization-design.md  (#43 草案, 待实现)
│
├── todo/                               ← 未开始任务的方案/设计文档
│   └── client-sdk-facility-design.md           (Phase 1 蓝图, spec 治理先行)
│
└── content-strategy/                   ← 独立参考：内容策略手册
    ├── content-strategy-handbook.md
    └── weekly-editorial-checklist.md
```

**生命周期约定**：设计文档按其关联任务的进展在子目录间流转——`todo/` → `in-progress/` → `done/`。顶级文件（plan/research/audit）是长期锚点，不随单个任务流转。

## 文档索引（按状态）

### 顶级锚点（长期规划）

| 文档 | 作用 | 日期 |
|------|------|------|
| [productization-research.md](productization-research.md) | 产品化调研报告（市场/竞品/路线选择）— 已于 2026-08-05 校准现状信息 | 2026-08-04（校准 08-05） |
| [productization-evolution-plan.md](productization-evolution-plan.md) | 产品化演进方案（路线图、优先级、风险） | 2026-08-05 |
| [iam-architecture-audit.md](iam-architecture-audit.md) | IAM 业务架构调研报告（14 业务域，逐条 file:line 验证） | 2026-08-09（修正 08-11） |
| [progress-status.md](progress-status.md) | 进展总览——全 phase 工作项的 完成度/状态/阻塞 速查表 | 2026-08-11 |
| [next-phase-implementation-plan.md](next-phase-implementation-plan.md) | 下一阶段详细实施计划（结合 IAM 缺口 + 演进路线） | 2026-08-11 |

### 已完成（done/）

| 文档 | 对应任务 | 完成日期 | 产出 |
|------|----------|----------|------|
| [oauth-oidc-compliance-audit.md](done/oauth-oidc-compliance-audit.md) | OAuth/OIDC 合规审计 | 2026-08-09 | 31 项偏差（F-001..F-031）全部修复（PR #44），含二次复扫 R-1/R-4/R-5 |
| [oauth-oidc-compliance-audit-plan.md](done/oauth-oidc-compliance-audit-plan.md) | 合规审查计划 | 2026-08-07 | 14 RFC × ~90 检查点的方法论 |
| [async-refactor-assessment.md](done/async-refactor-assessment.md) | 异步回调评估 | — | 评估报告（决策：暂不动手；若动手先做 GitHubController 示范） |

### 进行中（in-progress/）

| 文档 | 对应任务 | 已完成 | 待做 | GitHub Issue |
|------|----------|--------|------|-------------|
| [benchmark-facility-design.md](in-progress/benchmark-facility-design.md) | Phase 0 性能基准设施 | M1（skeleton + S1/S2 场景，已验证 green） | M2–M4（S3–S6 场景 + 竞品对比 + 报告） | — |
| [postgres-redis-cache-design.md](in-progress/postgres-redis-cache-design.md) | #42 Postgres+Redis 缓存层 | Phase 1（client-cache decorator, PR #47） | Phase 2（token cache）+ Phase 3（移除独立 Redis 模式） | #42 |
| [resource-scope-authorization-design.md](in-progress/resource-scope-authorization-design.md) | #43 资源-作用域授权模型 | 草案设计完成 | 全部实现 | #43 |

### 未开始（todo/）

| 文档 | 对应任务 | 前置依赖 | GitHub Issue |
|------|----------|----------|-------------|
| [client-sdk-facility-design.md](todo/client-sdk-facility-design.md) | Phase 1 多语言客户端 SDK | Layer 1 spec 治理（OpenAPI 单一源 + oasdiff 门）先行 | #41（OpenAPI bug） |

## 与相关文档的关系

- **research**（调研报告）：市场/竞品/路线选择的**输入**。其性能数字（§3.1）目前是工程估算，待 benchmark 设施验证。
- **evolution-plan**（演进方案）：对调研报告的**复盘 + 演进**，把"建立性能证据基线"提到最高优先级（Phase 0）。
- **iam-architecture-audit**（IAM 审计）：基于代码穷尽验证的**业务能力清单**，是 enterprise 功能缺口优先级排序的依据。
- **progress-status**（进展总览）：所有工作项的**实时状态快照**——开始新工作前先查此文件确认当前状态。
- **next-phase-implementation-plan**（下一阶段计划）：结合 IAM 缺口 + 演进路线的**近可执行行动项**。

## 落地路径

各设计文档中的具体工作项（benchmark milestone、spec 治理、客户端 milestone、企业协议实现等）应各自立项到 `openspec/changes/` 或 `.kiro/specs/`。本目录只做规划层锚点与设计记录。
