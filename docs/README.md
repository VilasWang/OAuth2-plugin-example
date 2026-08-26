# 文档中心

按受众组织（治理标准见 [documentation-governance.md](documentation-governance.md)）。
**评估**=想了解这个项目是什么；**集成**=要把 Fulla 接入你的系统；**运维**=要部署与保障它；
**贡献**=要给它提代码/文档；**档案**=决策与历史记录。

## 评估（Evaluate）

- [架构总览](backend/architecture-overview.md) — 技术栈、模块布局、授权码时序、存储策略
- [安全架构](backend/security-architecture.md) — 威胁模型、token 生命周期、密钥与哈希、安全头与限流
- [性能对比](../benchmarks/competitors/results/COMPARISON.md) — 与 Keycloak/Ory/Zitadel 的同环境五场景对比
- [对比方法论](benchmark/competitor-benchmark-design.md) — 上述对比的公平性规则与复现方式

## 集成（Integrate）

**C++ SDK（嵌入）**
- [SDK 集成指南](backend/sdk-integration-guide.md) — find_package、包矩阵、Drogon 宿主快速上手、验签与校验
- [SDK 运行时契约](backend/sdk-runtime-contract.md) — 线程模型、ABI 政策、异常与日志约定

**HTTP API（任意语言）**
- [API 参考](backend/api-reference.md) — 端点详解与错误码全表（权威契约：[openapi.yaml](../apps/server/openapi.yaml)）
- [OIDC 集成指南](backend/oidc-guide.md) — discovery/JWKS/id_token 验签、RP 登出
- [社交登录指南](backend/social-login.md) — GitHub（完整接线）/ Google / 微信
- [RBAC 与访问控制](backend/rbac-guide.md) — 角色与 scope 双闸模型

## 运维（Operate）

- [生产部署](ops/deployment.md) — Linux 全流程（域名/证书/SMTP/性能调优/安全清单）
- [Docker 部署](backend/docker-deployment.md) — compose 三形态、命名规范、调试环境、自动化验证
- [Windows / Docker Desktop](ops/deployment-windows-docker-desktop.md) — 本地验证路线
- [配置指南](backend/configuration-guide.md) — FULLA_* 环境变量、存储后端、issuer、缓存块
- [可观测性](backend/observability.md) — Prometheus 指标、审计日志、日志级别
- [账号锁定运维](ops/account-lockout.md) — 锁定规则与四种重置方法
- [PostgreSQL 大版本升级](ops/postgresql-major-upgrade.md) — dump/restore 与 pg_upgrade 双路线
- [部署验收清单](ops/verification-checklist.md) — 5 分钟 / 30 分钟两档验证

## 贡献（Contribute）

- [测试指南](backend/testing-guide.md) — 五层测试、执行方式、覆盖率方法论
- [CI/CD 指南](backend/ci-cd-guide.md) — 三门体系、平台矩阵、本地复现
- [版本与发版](backend/versioning-and-release.md) — SemVer 方案、发版 SOP
- [文档治理](documentation-governance.md) — 入库判据与文档站内容源设计
- 前端测试：[Admin 用例矩阵](admin/test-cases.md) · [User 用例矩阵](frontend/test-cases.md) · [E2E 方法论](admin/e2e-testing-guide.md)

## 档案（Archive）

- [ADR 决策记录](adr/) — 12 篇现行架构决策（SDK 分层、ErrorCatalog、Opaque token、协程排除等）
- [历史设计归档](history/) — 已冻结的设计文档（README 声明其口径为写作当时）
- [DDD 领域模型提案](backend/ddd-domain-model.md) — 未评审的未来演进底稿
- [更名决策记录](branding/rename-impact-fulla.md) · [入库标准审计](branding/repo-professionalization-audit.md)
- [OAuth/OIDC 合规尽调报告](productization-evolution/done/oauth-oidc-compliance-audit.md) — 31 项偏差全修复（2026-08-07 基线）

## 本地维护区（不入库）

过程性文档（productization-evolution、域名调研、各设计 tasks/plans 等）已按治理标准转为本地维护，
不入版本库；判据见 documentation-governance.md §一。
