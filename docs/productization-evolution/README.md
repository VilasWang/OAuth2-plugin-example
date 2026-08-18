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
│   ├── resource-scope-authorization-design.md  (#43 已实现, commit f04d3ba)
│   └── async-refactor-assessment.md            (异步回调评估报告)
│
├── in-progress/                        ← 进行中任务的方案/设计文档
│   ├── benchmark-facility-design.md            (Phase 0, M1–M4 已交付, 竞品对比待做)
│   └── postgres-redis-cache-design.md          (#42 Phase 1+2 已交付, Phase 3/4 待做)
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
| [productization-research.md](productization-research.md) | 产品化调研报告（市场/竞品/路线选择）— §3.1 已用 Phase 0 实测数据标注 | 2026-08-04（校准 08-05, 实测 08-12） |
| [productization-evolution-plan.md](productization-evolution-plan.md) | 产品化演进方案（路线图、优先级、风险） | 2026-08-05 |
| [iam-architecture-audit.md](iam-architecture-audit.md) | IAM 业务架构调研报告（14 业务域，逐条 file:line 验证） | 2026-08-09（修正 08-11/08-13） |
| [progress-status.md](progress-status.md) | 进展总览——全 phase 工作项的 完成度/状态/阻塞 速查表 | 2026-08-13 |
| [next-phase-implementation-plan.md](next-phase-implementation-plan.md) | 下一阶段详细实施计划（结合 IAM 缺口 + 演进路线） | 2026-08-13 |

### 已完成（done/）

| 文档 | 对应任务 | 完成日期 | 产出 |
|------|----------|----------|------|
| [oauth-oidc-compliance-audit.md](done/oauth-oidc-compliance-audit.md) | OAuth/OIDC 合规审计 | 2026-08-09 | 31 项偏差全部修复（PR #44），含二次复扫 |
| [oauth-oidc-compliance-audit-plan.md](done/oauth-oidc-compliance-audit-plan.md) | 合规审查计划 | 2026-08-07 | 14 RFC × ~90 检查点的方法论 |
| [resource-scope-authorization-design.md](done/resource-scope-authorization-design.md) | #43 资源-作用域授权模型 | 2026-08-12 | 完整实现：声明式 scope 注册表 + implication + DB 驱动（f04d3ba） |
| [async-refactor-assessment.md](done/async-refactor-assessment.md) | 异步回调评估 | — | 评估报告（决策：暂不动手） |

### 进行中（in-progress/）

| 文档 | 对应任务 | 已完成 | 待做 | GitHub Issue |
|------|----------|--------|------|-------------|
| [benchmark-facility-design.md](in-progress/benchmark-facility-design.md) | Phase 0 性能基准设施 | M1–M4 全部完成（6 场景 × 7 并发档 + 承重验证报告） | Phase 0.5 竞品对比 + 冷启动/内存精确测量 | — |
| [postgres-redis-cache-design.md](in-progress/postgres-redis-cache-design.md) | #42 Postgres+Redis 缓存层 | Phase 1（client-cache）+ Phase 2（token cache）已交付 | Phase 3（移除独立 Redis 模式）+ Phase 4（consent cache） | ~~#42~~ 已关闭 |

### 未开始（todo/）

| 文档 | 对应任务 | 前置依赖 | GitHub Issue |
|------|----------|----------|-------------|
| [openapi-spec-governance-plan.md](todo/openapi-spec-governance-plan.md) | Phase 1 Layer 1 spec 治理（M0） | ✅ 已执行完毕（PR #63 合并，v1.2.0）——文件暂存 todo/，待归档 done/ | ~~#41~~ 已关闭 |

### 进行中（in-progress/）

| 文档 | 对应任务 | 状态 | 下一步 |
|------|----------|------|--------|
| [client-sdk-facility-design.md](in-progress/client-sdk-facility-design.md) | Phase 1 多语言客户端 SDK | M0（spec 治理）已合并 v1.2.0；M1 Python + M2 Go + M3 接线 + M4 文档已实现（2026-08-18，待合并） | PyPI 项目注册 + secret 配置；发布后 AC7 冒烟 |
| [client-sdk-implementation-plan.md](in-progress/client-sdk-implementation-plan.md) | 客户端 SDK 实施计划（M1-M4） | 执行中（2026-08-18） | 随 PR 合并收尾 |

## 与相关文档的关系

- **research**（调研报告）：市场/竞品/路线选择的**输入**。§3.1 性能数字已用 Phase 0 实测数据标注。
- **evolution-plan**（演进方案）：对调研报告的**复盘 + 演进**，把"建立性能证据基线"提到最高优先级（Phase 0）。
- **iam-architecture-audit**（IAM 审计）：基于代码穷尽验证的**业务能力清单**，是 enterprise 功能缺口优先级排序的依据。
- **progress-status**（进展总览）：所有工作项的**实时状态快照**——开始新工作前先查此文件确认当前状态。
- **next-phase-implementation-plan**（下一阶段计划）：结合 IAM 缺口 + 演进路线的**近可执行行动项**。

## 落地路径

各设计文档中的具体工作项（benchmark milestone、spec 治理、客户端 milestone、企业协议实现等）应各自立项到 `openspec/changes/` 或 `.kiro/specs/`。本目录只做规划层锚点与设计记录。
