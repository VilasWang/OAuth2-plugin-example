# AuthForge 产品化演进方案

> **版本**: v1.0
> **日期**: 2026-08-05
> **文档性质**: 演进方案（规划层锚点，非逐条实施任务）
> **前置输入**: [../productization-research.md](../productization-research.md)（产品化调研报告，2026-08-04）
> **代码库版本**: v1.0.0 (MIT), branch `refactor/github-controller-callback-flattening`

---

## 零、如何阅读本文

本方案是对 [产品化调研报告](../productization-research.md) 的**复盘 + 演进**，而非替代。阅读顺序建议：

1. **[一、复盘]**：先看调研报告中哪些结论可靠、哪些与代码库现状脱节、最大风险何在。
2. **[二、战略定调]**：再看演进方案的核心主张（一句话 + 三原则）。
3. **[三、校准路线图]**：路线图本身，**已按"证据先于传播"原则重新排序**——把"建立性能证据基线"提到最前。
4. **[四、工作流分解]**：按系统/责任切分，便于落地。
5. **[五、风险矩阵]** 与 **[六、立即动作]**。

> 凡涉及具体实现的工作项（benchmark 设施、SAML/LDAP/SCIM、文档站等），应各自按本项目既有的 `openspec/changes/` 或 `.kiro/specs/` 规范流程立项；本文档只做规划层锚点与优先级裁定。

---

## 一、调研报告复盘：哪些对、哪些要修正

### 1.1 结论可靠

| 报告结论 | 评估 |
|----------|------|
| 路线 D（Open Core + SDK 许可优先，云托管远期） | **正确**。C++ SDK 嵌入能力（`find_package(authforge-*)` × 8 个分层包 + 源码级 SemVer，由 `tools/api-diff` 在 CI 强制）是真实存在且独有的差异化 |
| 目标客户细分（IoT/边缘、金融科技、高合规行业、C++ 技术栈企业） | **合理**。这些正是"低资源 + 无 GC 抖动 + 可审计源码"能打动的人群 |
| 定价参照（对标 RHSSO $1,000/年/实例、Auth0 按用户计费痛点） | **逻辑成立** |
| Open Core 功能边界（社区版核心 + Admin + Docker/Helm；企业版锁 SAML/LDAP/SCIM/多租户/审计） | **方向正确** |

### 1.2 报告与代码库现实脱节（需修正）

报告把若干**代码库已落地**的东西列成了 P0/P1 待办，会误导资源投放。逐一核对：

| 报告列为待办 | 代码库实际状态 | 处置 |
|--------------|----------------|------|
| §3.2「供应链安全：cosign 签名 + SBOM + SDK 校验和」（作为卖点） | **已落地**：`.github/workflows/release.yml` 里 cosign keyless 签名 manifest digest + syft 生成每镜像 SPDX SBOM；SDK tarball 带 `.sha256` | 从"待办"挪到 **§1.4 已有资产**，写进对外卖点 |
| §六 P0「SDK 文档站 / API 参考」 | **部分已有**：`docs/backend/sdk-integration-guide.md` + `docs/backend/sdk-runtime-contract.md` 已达发布级质量 | 待办收敛为"**独立文档站**（站点化 + 版本切换）"，而非从零写文档 |
| §六 P1「Helm Chart 优化」 | **已有** `deploy/helm/authforge`：含 pre-install/pre-upgrade migration-job 钩子、Chart version 与 appVersion 联动（Task 37）、values-local | 待办收敛为"**默认安全配置 + 一键部署**优化"，而非新建 chart |
| §六 P0「Benchmark 可复现」 | **真空白**：`tests/performance/benchmark/` 只有一个 `SubjectGenerator` 微基准（进程内、纳秒级），**无任何 HTTP 级 / 竞争性基准** | **保留为最高优先级**（见 §三 Phase 0） |
| §3.2「多语言 SDK」 | C++ 原生 SDK 已就绪；非 C++ 客户端缺失 | 保留，但明确为"**HTTP 客户端 SDK（OpenAPI 生成）**"而非原生 SDK；⚠️ 实际落地需先治 spec（见 [客户端 SDK 设计](../in-progress/client-sdk-facility-design.md)） |

### 1.3 报告最大的风险（演进方案必须正面回应）

报告反复宣称：单机 **10 万+ QPS**、**P99 < 2ms**、**内存 50–120MB**、**零 GC 抖动**（§3.1 表格、§3.2、§5.3 信息矩阵全部引用）。

**这些数字目前是断言，不是测量。** 代码库里没有：

- AuthForge 自身的 HTTP 端到端压测（`/oauth2/token` 各 grant、`/oauth2/introspect`、`/oauth2/userinfo`、`/.well-known/*` 在真实 Drogon + PostgreSQL + Redis 栈下的 QPS / 延迟 / 内存）；
- 与 Keycloak / Ory / Zitadel 的**同环境对比**；
- 第三方可一键复现的压测脚本与数据发布流程。

**后果**：整个产品定位 = "身份基础设施的 C++ 极速"。如果这组数字无法被独立验证，甚至被证伪，差异化叙事就会崩塌——而且一旦对外发布了不实的性能声明（README 徽章、博客、TechEmpower），信誉受损不可逆。

> **这是演进方案的 P0 中的 P0：先把"快"做成可以被任何人复现的硬证据，再谈对外传播与商业化。**

### 1.4 已有资产清单（报告应纳入卖点）

下列能力已达商业级，是**现成的对外卖点**，无需重复投入：

| 资产 | 位置 | 价值 |
|------|------|------|
| 多架构镜像 + cosign 签名 + SPDX SBOM | `.github/workflows/release.yml`（Task 38） | 供应链安全卖点（报告 §3.2 已列，但误为待办） |
| 源码级 SemVer 守护 | `tools/api-diff/api_diff.py` + CI static-checks 门 | 企业采购关心的 API 稳定性承诺 |
| 架构分层守护 | `tools/arch-guard/arch_guard.py` | Domain 层永不依赖 Drogon；可嵌入性的工程证据 |
| 迁移卫生守护 | `tools/migration-check/migration_check.py` | DB schema 变更可控 |
| SDK 打包（install-tree + build-tree `find_package`） | `cmake/AuthForgePackage.cmake` | 8 包 + 传递依赖闭包，`examples/full-stack-host` 验证 |
| Helm chart + migration-job 钩子 | `deploy/helm/authforge/` | 生产级 K8s 部署 |
| 三平台 CI（Linux/Windows/macOS） | `.github/workflows/_build-test.yml` | 跨平台可信度 |

---

## 二、战略定调

> **一句话：先把"快"做成可以被任何人复现的硬证据，再谈 Open Core 与 SDK 商业化。**

三原则：

1. **证据先于传播**（evidence before claims）——任何对外性能声明必须有可复现脚本 + 公开数据页支撑，否则不写进 README / 博客 / 定价页。在拿到证据前，现有数字一律标注"目标值，待 benchmark 验证"。
2. **Open Core，企业功能上锁**——社区版 MIT 维持核心引擎 + Admin Console + User Frontend + Docker/Helm；企业版锁 SAML / LDAP / SCIM / 多租户增强 / 审计合规。社区/企业边界用 Conan/CMake option 门控（沿用现有 `with_identity` / `with_social` / `with_webauthn` 先例）。
3. **SDK 许可作为独特收入轴**——这是竞品结构性无法复制的（Keycloak/Auth0/Ory 都不是可嵌入 C++ 库）。路径 C（SDK 商业许可）与路径 A（Open Core）并行，云托管（路径 B）远期。

---

## 三、校准后的演进路线图

> 对报告 §六路线图**重新排序**：把"建立性能证据基线"独立成 Phase 0，提前到所有对外动作之前，因为它阻塞几乎所有对外传播与商业化叙事。

### Phase 0：可信度基线（0–2 个月，最高优先级）

**目标**：把报告 §3.1/§3.2/§5.3 的所有性能断言，从"断言"变成"可复现的测量"。

> **落地蓝图**：本阶段的完整技术设计见 [基准设施设计文档](in-progress/benchmark-facility-design.md)（场景矩阵、测试策略、验收标准、4 个实施 milestone）。下表是规划层摘要。

| 优先级 | 工作项 | 验收标准 |
|--------|--------|----------|
| **P0** | **建立 HTTP 端到端性能基准设施**：用 **wrk** 对 `/oauth2/token`（authorization_code+PKCE / client_credentials / refresh_token）、`/oauth2/introspect`、`/oauth2/userinfo`、`/.well-known/openid-configuration`、`/.well-known/jwks.json` 压测；在真实 Drogon + PostgreSQL + Redis 全栈下产出 QPS / P50/P95/P99 / 稳态内存 / CPU 报告 | 脚本进仓库 `benchmarks/`；本地一键跑通；首次数据落盘 |
| **P0** | **自托管竞品对比基准**：在**同硬件、同并发参数、同后端（PG/Redis）配置**下压 Keycloak、Ory（Hydra+Kratos）、Zitadel，产出对比报告 | 三家均在本机跑通；对比表含 QPS / P99 / 内存 / 冷启动 |
| **P0** | **一键复现脚本 + 公开数据页**：脚本进仓库，数据发布到 GitHub Pages 或文档站 | 第三方按 README 指引可在自己机器上复现误差范围内的数据 |
| P1 | **证伪/证实关键子声明**：明确"内存占用""P99 抖动（长时间运行的 tail latency）""GC 抖动对比"在真实全栈下的实际值，**反向修正报告 §3.1 表格** | 更新后的报告表格每行标注"实测 / 目标" |

> **重要**：Phase 0 的产出会反向修正报告 §3.1/§3.2/§5.3 的数字。如果实测发现某些维度并不领先（很可能 token 验签的 OpenSSL 开销、PG 往返会让"<2ms P99"在非极端配置下不成立），就要**诚实地把卖点收敛到真正领先的维度**（如稳态内存占用、长时间运行的尾延迟稳定性、QPS/瓦）。诚实优于夸大。

### Phase 1：产品化基础 + 社区启动（2–6 个月）

**依赖**：Phase 0 数据（否则对外传播无据可依）。

> **落地蓝图**：本阶段「多语言 HTTP 客户端 SDK」的完整技术设计见 [客户端 SDK 设计文档](../in-progress/client-sdk-facility-design.md)。⚠️ 该设计调研发现 OpenAPI spec 当前处于"两源漂移 + 一个死文件"状态，且 api-diff 只管 C++ 头不碰 HTTP 面——故客户端生成的**前置地基是 spec 单一源治理 + 破坏性变更门**（Layer 1），必须先于客户端生成（Layer 2）。下表是规划层摘要。

| 优先级 | 工作项 | 依赖 |
|--------|--------|------|
| **P0** | **独立文档站**（Docusaurus 或 VitePress，中英双语 + 版本切换）：把 `docs/backend/*` 的 SDK 集成指南、运行时契约、API 参考、架构概览搬上站 | 复用已有文档；新增站点工程 |
| **P0** | **多语言 HTTP 客户端 SDK**：先用 OpenAPI spec 生成 Python + Go 客户端（非原生 SDK，只是 HTTP 包装） | **spec 治理地基先行**（见 [客户端 SDK 设计](../in-progress/client-sdk-facility-design.md) Layer 1） |
| **P0** | **首发技术博客 + 基准报告发布**：HN / Reddit r/cpp / r/netsec / Drogon 社区同步；标题候选「为什么我们用 C++ 构建 OAuth2 服务器」+ 实测对比报告 | Phase 0 数据 |
| P1 | README 性能徽章 + benchmark 链接（带"如何复现"小节） | Phase 0 数据 |
| P1 | TechEmpower Framework Benchmarks 提交 | Phase 0 数据 |
| P1 | **SDK 商业化试点**：寻找 2–3 个种子客户（IoT / 量化交易 / 嵌入式）验证 SDK 嵌入需求与定价 | 报告 §八-7 |

### Phase 2：企业版（6–12 个月）

| 优先级 | 工作项 | 备注 |
|--------|--------|------|
| **P0** | **SAML 2.0**（企业 SSO：ADFS / Azure AD / Okta 作为 SP） | 传统企业采购硬门槛；体量大，建议做客户驱动的最小子集先行 |
| **P0** | **LDAP/AD 联邦**：用户目录同步 | 同上 |
| **P0** | **SCIM 2.0**：用户生命周期自动化（create/update/deactivate 同步到下游 SP） | B2B SaaS 必需 |
| **P0** | **多租户增强**：组织/租户隔离 + 管理委托（当前多租户能力需先评估是否够"企业级"） | 先做差距分析 |
| P1 | **ABAC**（属性访问控制）：补充现有 RBAC | 已有 RBAC 基础（见 `rbac-guide.md`） |
| P1 | **审计合规报告**导出（GDPR / 等保） | |
| P1 | **SDK 商业版**：Redis Cluster / PostgreSQL HA 存储适配器 | 路径 C 收入 |
| P2 | **第三方安全审计 + SOC2 准备** | 企业采购门槛；安全流程文档化 |

### Phase 3：云托管探索（12–24 个月，谨慎）

**启动门槛**（建议硬性指标，达标前不投入）：自托管付费客户 ≥ N（N 待定，建议 ≥ 5）且有明确"不愿自运维"的获客信号。

| 优先级 | 工作项 | 备注 |
|--------|--------|------|
| **P0** | AuthForge Cloud MVP：多租户云平台 + 自助注册 | C++ 在云原生多租户环境需额外适配（二进制镜像、隔离） |
| **P0** | 多区域部署：数据驻留选择 | 满足 GDPR / 等保数据本地化 |
| P1 | 自助式 dashboard：用量监控 + 计费 | |
| P1 | AWS / Azure / GCP Marketplace 集成 | |
| P2 | AI Agent 身份管理：M2M token 管理 + Agent 身份注册 | 报告 §2.1 趋势；远期 |

> 报告把云托管列为远期是对的。**C++ 部署在云原生多租户环境 ROI 不确定性高**（二进制镜像、多区域数据驻留、计费），先用自托管 + SDK 许可验证收入，再谨慎投入。

---

## 四、工作流分解（按系统 / 责任切分）

便于落地与责任分配。每个工作流的产出应可独立验收。

### 4.1 Benchmark 工作流（Phase 0 核心）

- **新建 `benchmarks/` 顶级目录**（与 `tests/performance/` 区分：后者是回归测试，前者是对外可复现基准）。
- 组成：
  - `benchmarks/authforge/` — 自压测脚本（wrk/k6 lua + docker-compose 起完整栈）
  - `benchmarks/competitors/` — Keycloak / Ory / Zitadel 的同环境压测脚本
  - `benchmarks/results/` — 历次数据（含硬件/配置/日期），可被文档站引用
  - `benchmarks/README.md` — 一键复现指引
- **CI 集成**：加一个轻量回归工作流（如 `benchmarks-regression.yml`），PR 触发自压测的子集，防止性能悄悄退化（不跑竞品对比，太慢）。

### 4.2 文档站工作流（Phase 1）

- 选型：建议 **Docusaurus**（中英双语 + 版本化 + React 生态成熟）。
- 源就在 `docs/`，站点工程放 `docs/site/` 或独立仓。
- 核心页：SDK 集成指南、SDK 运行时契约、API 参考、架构概览、benchmark 数据页。
- 与 release 联动：每个 tag 生成一个版本快照。

### 4.3 多语言客户端工作流（Phase 1）

> 完整设计见 [客户端 SDK 设计文档](../in-progress/client-sdk-facility-design.md)。

- **前置地基**（Layer 1）：OpenAPI spec 单一源治理。调研发现 spec 当前"两源漂移 + 一个死文件"，且 `tools/api-diff` **只管 C++ 头、不碰 HTTP 面**——故需先定 YAML 单源、删死孤儿、加 YAML↔代码一致性门 + oasdiff 破坏性变更门。
- **客户端生成**（Layer 2）：从 spec 生成 Python（openapi-python-client）/ Go（oapi-codegen）客户端，手写 auth 层，提交进 `clients/`，漂移门守护。
- 发布到 PyPI / Go module proxy，与项目 SemVer 联动。

### 4.4 企业功能工作流（Phase 2）

- 每个企业协议（SAML / LDAP / SCIM）按本项目既有的 `openspec/changes/` 或 `.kiro/specs/` 规范流程立项：先 spec（含协议合规矩阵），再实现，再测试。
- 社区版/企业版边界用 Conan/CMake option 门控（沿用 `with_identity` 等先例），保证社区版 MIT 源码自洽可编译。
- 企业协议实现体量大，**先做客户驱动的最小子集**（如 SAML 先做 SP-initiated SSO 的 IdP 侧），避免一次性铺开。

### 4.5 商业许可工作流（Phase 1–2，法务 + 工程）

- 明确社区版（MIT）vs 商业许可代码边界，必要时引入 `LICENSE-enterprise` 与双授权（如 AGPL/Business License 给企业版）。
- SDK 版本兼容性矩阵 + LTS 策略（v1.x 仅承诺源码级 SemVer，不承诺 ABI——见 `sdk-runtime-contract.md` §2）。
- OEM 许可条款（无限制嵌入 + 源码访问）。

---

## 五、风险矩阵

在报告 §七基础上的补充与修正：

| 风险 | 报告评估 | 本方案补充 / 修正 |
|------|----------|-------------------|
| **性能声明未被验证 / 被证伪** | **未提及** | **最高风险**。Phase 0 必须先做；若某维度不领先，立即修正卖点，**绝不对外夸大**。在证据出来前，README/报告里的具体数字加"目标值，待 benchmark 验证"标注 |
| C++ 社区贡献门槛高 | 高 | 多语言 HTTP 客户端（§4.3）是直接缓解；OpenAPI 生成客户端让非 C++ 用户也能用 |
| Keycloak 品牌势能强 | 高 | 聚焦性能差异化的细分市场（IoT / 金融科技）；不拼功能清单 |
| Auth0/Okta 功能碾压 | 中 | 拼性能 / 成本（自托管固定成本 vs 按用户计费）/ 嵌入能力 |
| MIT 被白嫖 | 低 | 同意报告：企业功能 + SDK 商业许可锁定高价值；OSS 核心被白嫖反而是分发渠道 |
| 企业版功能开发成本 | 中 | SAML/LDAP/SCIM 体量大，先做客户驱动的最小子集，避免一次性铺开 |
| 现有发布能力被低估 | —（报告未提） | **修正**：cosign/SBOM/api-diff SemVer/Helm migration job 已是商业级，应写进对外卖点（§1.4） |

---

## 六、立即动作（本周 / 本 sprint）

1. **新建 `benchmarks/` 目录骨架**（详见 [基准设施设计文档](in-progress/benchmark-facility-design.md)），先搭 AuthForge 自身 HTTP 压测（wrk 脚本 + docker-compose 起完整栈），跑出第一组 `/oauth2/token` QPS / 延迟数据——这是验证一切的前提。
2. **盘点并冻结"对外性能声明"**：在 Phase 0 数据落地前，`README.md` / `README.zh-CN.md` / 本目录 `productization-research.md` 里凡是写具体 QPS / 延迟 / 内存数字处，加"目标值，待 benchmark 验证"标注，避免对外撒谎。
3. **更新 `productization-research.md`**（✅ 已于 2026-08-05 完成）：已把 §3.2/§六里已落地的能力（cosign/SBOM、Helm、SDK 打包、api-diff SemVer）从"待办"挪到"已有资产"（§3.4），并在 §一/§3.1/§3.2 插入承重假设风险段。
4. **确定文档站技术选型**（建议 Docusaurus）并立项到 `openspec/changes/` 或 `.kiro/specs/`。

---

## 附录 A：与调研报告的章对应关系

| 调研报告章节 | 本方案对应 | 关系 |
|--------------|-----------|------|
| §一 执行摘要（Open Core + 双轨） | §二 战略定调 | 精炼为一句话 + 三原则 |
| §三 竞争定位（§3.1 性能表 / §3.2 卖点） | §一 1.3 / §三 Phase 0 | **修正**：性能数字待实测；部分卖点（cosign/SBOM）已落地 |
| §四 路线选择（A/B/C/D） | §二 原则 2/3 + §三 | 沿用 D（A+C 优先，B 远期），但插入 Phase 0 |
| §六 路线图（Phase 1/2/3） | §三 Phase 0/1/2/3 | 重新排序，插入 Phase 0 |
| §七 风险 | §五 风险矩阵 | 补充"性能声明未验证"为最高风险 |
| §八 行动建议 | §六 立即动作 | 收敛为本 sprint 可执行项 |

## 附录 B：已有资产在代码库中的位置速查

| 资产 | 文件 |
|------|------|
| 版本单一源 | `cmake/Version.cmake` |
| SDK 打包 | `cmake/AuthForgePackage.cmake`，各 `libs/*/CMakeLists.txt` |
| Release 流水线（cosign + SBOM + 多架构） | `.github/workflows/release.yml` |
| CI 三平台 + SDK smoke | `.github/workflows/ci.yml`、`_build-test.yml`、`_sdk-smoke.yml` |
| API 面 SemVer 守护 | `tools/api-diff/` |
| 架构分层守护 | `tools/arch-guard/` |
| 迁移卫生守护 | `tools/migration-check/` |
| Helm chart | `deploy/helm/authforge/` |
| SDK 消费示例 | `examples/full-stack-host/`、`examples/third-party-host/` |
| SDK 集成文档 | `docs/backend/sdk-integration-guide.md`、`docs/backend/sdk-runtime-contract.md` |

---

*本方案基于调研报告、公开市场信息与代码库现状（v1.0.0）编制，供产品决策参考。Phase 0 数据落地后应回头修订性能相关章节。*
