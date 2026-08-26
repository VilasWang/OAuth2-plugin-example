# 文档治理设计 — 入库边界 · 目录重组 · Docusaurus 内容源

> 状态：**提案 v1**（2026-08-26），经批准后分三个 Phase 执行
> 原则一句话：**入库是给别人看的（stranger 能据此完成一件事）；留在本地是给维护者和 agent 看的（过程、状态、私人上下文）。**

## 一、入库判据（写进 documentation-standards 的硬标准）

一篇文档**入库**当且仅当满足至少一条：

1. **可执行**：stranger 依此完成一件事（部署、集成、排查、贡献）；
2. **可决策**：记录了别人理解系统所必需的设计决策及其理由（ADR）；
3. **可信任**：对外承诺（安全策略、兼容性承诺、版本政策、测量报告）。

不满足任何一条的过程性内容（计划、进度、调研、会话产物、内部经营记录）**留本地**。

## 二、现状盘点与分类（docs/ 全部 136 文件 + 根部文档）

| 去向 | 内容 | 动作 |
|---|---|---|
| **入库 · 前台**（站源） | `backend/` 的 architecture-overview、security-architecture、rbac-guide、sdk-integration-guide、sdk-runtime-contract、api-reference、configuration-guide、observability、data-consistency、data-persistence、testing-guide、ci-cd-guide、versioning-and-release、error-code 相关；`ops/` 全部 6 篇；`performance-optimization/` 7 篇；`admin/`、`frontend/` 指南 | 保留入库；重组为站结构（见 §四） |
| **入库 · 档案**（站源，Archive 区） | `history/design/` 里的高价值设计（并发生命周期审计、错误码标准化、repo 重构、http-integration 计划、已迁入的 6 个 kiro specs）；`branding/rename-impact-fulla.md`（改名决策记录）与 `repo-professionalization-audit.md`（入库标准依据，AGENTS.md 引用） | 改造为 `docs/adr/`（编号+状态+背景+决策+后果），kiro specs 是现成素材 |
| **留本地**（出库，磁盘保留） | `productization-evolution/` 全部 22 文件（plan/progress/todo/content-strategy/done——内部经营记录）；`branding/rename-candidates.md`（域名调研）；`history/PRD/` 的实施计划与任务清单（PROGRESS、implementation_plan、p0/p1 tasks、fix_macos_ci 等过程稿）；`history/design/superpowers/`（AI 会话产物） | `git rm --cached` + .gitignore 增补 `docs/productization-evolution/` 等路径 |
| **待删除**（逐个确认） | 疑似重复/被取代稿：`backend/docker-guide.md` vs `docker-deployment.md`（二选一）；孤立 openapi 规范副本（openapi-spec-topology 记忆登记的死孤儿）；`backend/api/` 下与 openapi.yaml 漂移的生成快照 | 确认后 `git rm` |
| **根部不动** | README ×2、CHANGELOG、CONTRIBUTING、SECURITY、TECH_SPECS、AGENTS、docs/README.md（重写为受众导航） | docs/README.md 重写 |

边界案例裁定：`history/PRD/` 里的**设计**类（admin_console_design、frontend_design、mfa_auth_code_pkce_design）——保留价值看是否仍反映现状；反映的转 ADR，失效的归档标注 "superseded"，纯过程的留本地。

## 三、执行 Phase 划分

- **Phase A（结构重组）**：建 `docs/adr/`（迁 6-10 篇 + 编号）；出库本地类；删死稿；重写 `docs/README.md` 为五区受众导航（评估 / 集成 / 运维 / 贡献 / 档案）。
- **Phase B（文档站骨架）**：`website/` 目录（Docusaurus 3）+ GitHub Pages 部署 workflow + 站结构搭建（§四）。
- **Phase C（内容补齐）**：三篇缺失深潜（session 管理、token 生命周期、multi-tenancy）+ 既有文档的 frontmatter/导航适配 + 双语策略落地。

## 四、Docusaurus 内容源设计（与治理合并考虑的核心）

**关键决策：站源 = 入库前台文档本体，不做第二份拷贝。** 单一事实源原则——`docs/` 重组后的结构就是站点结构，Docusaurus 用 sidebar 配置引用，绝不复制内容（复制必然漂移，正是本治理要消灭的问题）。

### 4.1 站点信息架构（受众路径）

```
website/（Docusaurus 3，GitHub Pages 部署）
├── docs/ → 内容源指向仓库 docs/（见 4.2 引用方式）
│   ├── intro/          # 项目定位、能力地图（复用 README 内容）、Quick Start
│   ├── architecture/   # 总览、安全架构、SDK 分层、模块地图
│   ├── domains/        # 认证 / 授权 / 访问控制 / 令牌生命周期 / 多租户（深潜，Phase C 补齐三篇）
│   ├── sdk/            # C++ 集成三件套、Python/Go 客户端
│   ├── operate/        # 部署（compose/Helm/Windows）、配置、可观测性、安全清单、账号锁定
│   ├── contribute/     # 构建、测试、CI/CD、版本与发版、文档标准
│   └── adr/            # 决策档案（只读历史）
├── API 渲染            # openapi.yaml → 站内 API 参考页（swagger-ui 或 redoc 组件页 + 源链）
├── benchmark 区        # COMPARISON.md + 方法论（可信任承诺类）
└── Blog（可选）        # 发版公告 = CHANGELOG 节选； Discussions Announcements 互链
```

### 4.2 内容源引用方式（三选一，推荐 a）

- **a. 目录即站源**：把 §二"前台"文档物理移动到 `docs/<区>/`（如 `docs/operate/deployment.md`），`website/docusaurus.config.js` 的 sidebar 直接指向仓库根 `../docs/`（Docusaurus 支持自定义 docs 路径）。**零拷贝、零漂移**；README 里的相对链接同步更新一次。
- b. symlink/脚本同步：Windows 仓库不可靠，弃。
- c. 双目录维护：必然漂移，弃。

### 4.3 双语策略

Phase B 先英文站（受众最大化）；中文沿用 GitHub 上 `README.zh-CN.md` + wiki（已上线）承载；Phase C 视流量决定是否启用 Docusaurus i18n（`website/i18n/zh/`，内容源同构）。

### 4.4 部署与守门

- `website/` 入库（主流做法，CNCF 系项目标配）；`node_modules/`、`website/build/` 进 .gitignore。
- `.github/workflows/website.yml`：paths 过滤 `docs/**` 与 `website/**`，PR 预览构建 + master 推送部署 GitHub Pages（与 fulla.dev 域名对接，注册后 CNAME）。
- 文档改动入口守门：`website.yml` 构建失败 = 死链/断图即刻暴露，替代人工检查。

### 4.5 明确不进站的内容

本地类（§二留本地行）、`docs/history/` 的过程稿、benchmarks 原始 JSON（报告进站、数据留库）、内部安全审计过程稿。

## 五、验收标准

1. `git ls-files docs` 中每篇文档能归入 §一 三条判据之一（抽查即过）；
2. 站点构建零死链，sidebar 五区导航完整；
3. README ×2 只做门面，任何深度内容都有 docs/ 落点；
4. 新增文档有唯一去处判断表（documentation-standards.md 更新版承载）。
