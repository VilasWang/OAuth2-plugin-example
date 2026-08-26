# 文档中心

按受众组织的**用户向**文档树（维护者过程文档在仓库外的 `docs-local/`，判据见
[documentation-governance.md](documentation-governance.md)）。本树同时是
[fulla.dev](https://fulla.dev) 文档站的唯一内容源（零拷贝）。

## 评估（Evaluate）

- [架构总览](architecture/architecture-overview) — 技术栈、模块布局、授权码时序、存储策略
- [安全架构](architecture/security-architecture) — 威胁模型、token 生命周期、密钥与哈希、安全头与限流
- [性能对比](../benchmarks/competitors/results/COMPARISON.md)（[方法论](benchmark/competitor-benchmark-design.md)）

## 集成（Integrate）

**C++ SDK（嵌入）**：[集成指南](sdk/sdk-integration-guide) · [运行时契约](sdk/sdk-runtime-contract)

**HTTP API（任意语言）**：[API 参考](domains/api-reference)（权威契约：
[openapi.yaml](../apps/server/openapi.yaml)）· [OIDC 集成](domains/oidc-guide) ·
[社交登录](domains/social-login) · [RBAC 与访问控制](domains/rbac-guide)

## 架构深潜（Architecture）

- [数据与持久化](architecture/data-persistence) — 存储分层、缓存一致性（延迟双删）、token 家族

## 运维（Operate）

[生产部署](operate/deployment) · [Docker 部署](operate/docker-deployment) ·
[Windows/Docker Desktop](operate/deployment-windows-docker-desktop) ·
[配置指南](operate/configuration-guide) · [可观测性](operate/observability) ·
[账号锁定](operate/account-lockout) · [PG 大版本升级](operate/postgresql-major-upgrade) ·
[部署验收清单](operate/verification-checklist)

## 贡献（Contribute）

[测试指南](contribute/testing-guide) · [CI/CD](contribute/ci-cd-guide) ·
[版本与发版](contribute/versioning-and-release) · [文档治理](documentation-governance.md) ·
前端测试：[Admin 用例](contribute/admin-test-cases) · [User 用例](contribute/user-frontend-test-cases) ·
[E2E 方法论](contribute/admin-e2e-testing-guide)

## 决策档案（ADR）

[docs/adr/](adr/) — 12 篇现行架构决策记录（SDK 分层、ErrorCatalog、Opaque token、协程排除等）。

> 更早期的完整设计档案（kiro specs 全文、PRD 设计、更名决策记录、合规尽调原稿）由
> git 历史与维护者本地保存；ADR 已提炼其仍有效的决策。
