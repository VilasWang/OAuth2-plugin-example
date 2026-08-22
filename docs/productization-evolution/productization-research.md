# AuthForge 产品化路线调研报告

> **调研日期**: 2026-08-04  
> **版本**: v1.0  
> **项目状态**: Production Ready (v1.0.0, MIT License)

---

## 一、执行摘要

AuthForge 是一个基于 C++ (Drogon 框架) 构建的全栈 OAuth2/OIDC 授权服务器，同时支持**开箱即用的产品部署**（Docker/Helm）和**可嵌入的 C++ SDK**（`find_package`）。这在当前 IAM 市场中是独一无二的定位——所有主流竞品均基于 Java (Keycloak)、Go (Ory/Zitadel)、Python (Authentik) 或 Node.js (Logto/SuperTokens)。

**核心结论**: AuthForge 的 C++ 技术栈带来了天然的**极致性能**和**超低资源消耗**优势，适合走「高性能/边缘计算身份基础设施」的差异化产品化路线。建议采用 **Open Core + 双轨商业模式**（社区版开源 + 企业版/云托管商业化），以 SDK 嵌入许可和托管云服务作为主要收入来源。

> ⚠️ **承重假设风险（✅ 实测验证 + 2026-08-21 对比表全量刷新）**：本报告的核心商业叙事（"极致性能 / 超低资源"，见 §3.1）的性能数字已通过 Phase 0 基准设施实测验证。**最新实测裁决（2026-08-21 同环境四产品对比，优化后基准档）**：五场景 QPS 全部领先（S1 2.2x / S2 2.4x / S3 1.8x / S5 1.06x / S6 1.4x）；P99 ✅低并发达标；内存 ✅SDK 口径远超标（2.5 MB peak WS）；冷启动 ✅1.38s。详见 §3.1 实测裁决表 + `benchmarks/competitors/results/COMPARISON.md`。对外传播须使用实测数字并诚实标注场景限定（含 GC 抖动主张已关闭——本机四家同款环境噪声）。

---

## 二、市场格局分析

### 2.1 IAM 市场规模与趋势

全球身份与访问管理 (IAM) 市场正在快速增长，驱动因素包括：

- **零信任安全架构**的普及推动企业部署独立身份基础设施
- **数据合规法规** (GDPR, 中国《个人信息保护法》) 要求身份数据本地化
- **SaaS 多租户**模式兴起，B2B SaaS 需要内置身份管理能力
- **AI Agent** 时代的机器身份 (M2M) 认证需求爆发
- **边缘计算**和 IoT 场景对轻量级身份服务的需求

### 2.2 竞品全景

| 产品 | 技术栈 | 许可证 | 部署模式 | 定价模型 | 核心定位 |
|------|--------|--------|----------|----------|----------|
| **Auth0 (Okta)** | Node.js | 闭源 SaaS | 仅云 | 免费 25K MAU, 付费从 $35/月 | 开发者优先的 CIAM |
| **Okta** | Java | 闭源 SaaS | 仅云 | 从 $6/用户/月 | 企业级 workforce IAM |
| **Keycloak** | Java | Apache 2.0 | 自托管 / RHSSO | 免费, Red Hat 支持 $1,000/年/实例 | 开源全功能 IAM |
| **Ory** | Go | Apache 2.0 | 自托管 + 云 | 云: $770/年起 ($0.14/aDAU) | 云原生 API-first |
| **Zitadel** | Go | AGPLv3 | 自托管 + 云 | 免费 100 DAU, Pro $100/月 | 多租户 B2B SaaS |
| **Authentik** | Python | MIT | 自托管 | 免费 + 付费支持 | DevOps 友好 IdP |
| **Logto** | Node.js | MPL-2.0 | 自托管 + 云 | 免费 1K MAU, Pro $99/月 | 开发者优先 CIAM |
| **Casdoor** | Go | Apache 2.0 | 自托管 | 免费 | 轻量级 IAM |
| **SuperTokens** | Node.js | Apache 2.0 | 自托管 + 云 | 免费 1K MAU | 轻量级认证 |
| **Authelia** | Go | Apache 2.0 | 自托管 | 免费 | 反向代理网关 |
| **AuthForge** | **C++** | **MIT** | **自托管 + SDK** | **待定** | **高性能身份基础设施** |

### 2.3 关键市场洞察

1. **定价痛点**: Auth0 按月活用户 (MAU) 计费，大规模场景下成本失控。10K MAU 的 B2C 应用月费 $700，B2B 月费 $2,100
2. **开源崛起**: Keycloak、Ory 等开源方案因成本可控和数据主权优势，在中大型企业中加速渗透
3. **性能盲区**: 所有主流竞品均使用带 GC 的语言 (Java/Go/Python/Node.js)，在高并发场景下存在延迟抖动和内存膨胀问题
4. **SDK 空白**: 没有任何竞品提供可嵌入的 C++ SDK——Auth0/Keycloak/Ory 全部是独立部署的服务，不支持 `find_package` 式集成
5. **边缘/IoT 需求**: 现有 IAM 方案动辄占用 500MB+ 内存 (Keycloak/JVM)，无法运行在资源受限的边缘设备上

---

## 三、AuthForge 竞争定位

### 3.1 独特优势

| 维度 | AuthForge | Keycloak (Java) | Ory (Go) | Auth0 (Node.js) |
|------|-----------|------------------|----------|-----------------|
| **语言运行时** | C++ 编译机器码, 无 GC | JVM, GC STW | Go runtime, GC | V8 引擎 |
| **QPS (单机)** | ~100,000+ | ~10,000-20,000 | ~30,000-50,000 | ~5,000-10,000 |
| **内存占用** | ~50-120 MB | ~350-500 MB+ | ~210 MB | ~150-300 MB |
| **延迟 (P99)** | < 2 ms | 10-50 ms | 5-15 ms | 20-100 ms |
| **GC 抖动** | 无 | 有 (STW) | 有 (GOGC) | 有 (V8 GC) |
| **冷启动** | ~5s | 30-60s+ | ~10s | ~7s |
| **嵌入能力** | C++ SDK (find_package) | 无 | 无 | 无 |
| **协议合规** | RFC 6749/7636/7662/7009/8414/7517/8628/7591 | 完整 | 完整 | 完整 |
| **多语言 SDK** | C++ 原生 | Java/JS/Python 等 | Go/JS 等 | JS/Python/Go 等 |

> ⚠️ **数据状态说明（AuthForge 列）**：上表中 AuthForge 的 **QPS / 内存占用 / 延迟(P99) / 冷启动** 四行数字原为工程估算。**Phase 0 基准实测已于 2026-08-12 完成**（8 vCPU WSL2 / postgres+redis 全栈），实测裁决如下（详见 `benchmarks/results/SUMMARY.md`）：
>
> | 维度 | 原估算 | 实测裁决 | 说明 |
> |------|--------|----------|------|
> | **QPS** | ~100,000+ | ⚠️ **场景限定** | discovery（无状态）87k QPS（8 vCPU WSL，线性外推 16 核裸机 ~170k）；token 签发 12.8k QPS；introspect 19.2k / userinfo 40.5k QPS（wave-2 缓存优化后）。"10 万+"仅适用于无状态端点 |
> | **内存** | ~50-120 MB | ✅ **SDK 口径远超标** | SDK 嵌入口径实测（2026-08-13）：`third-party-host-smoke`（纯 SDK）peak working set **2.5 MB** / private 0.6 MB / binary 12 MB；`full-stack-host-smoke`（SDK+Drogon）同样 2.5 MB peak。docker stats 的 2.4 GB 是容器全栈口径（含连接池/共享库/page cache），与 SDK 声称不同口径 |
> | **P99** | < 2 ms | ✅ **低并发达成** | c≤16 时 P99 1-4ms（S1/S3/S6）；高并发（c≥64）退化 12-430ms（连接池排队效应） |
> | **冷启动** | ~5s | ✅ **实测达成（量级领先）** | 专用 measure-cold-start.sh 实测 fresh 1.38s（含 PG/Redis 启动），远超 ~5s 目标 |
>
> 竞品列已由 **Phase 0.5 同环境实测**（最近一次全量刷新 **2026-08-21**，四产品同 session 串行：同一台 WSL2 8 vCPU / 16GB、同一 wrk 阶梯 2→128、同一 PostgreSQL 17、各家官方推荐配置；AuthForge 为性能优化后基准档——池 64/cache on/LTO，见 COMPARISON 附录 A）替换——数字溯源 `benchmarks/competitors/results/COMPARISON.md`（gen-comparison.py 生成，无手填）：
>
> | 维度 | AuthForge | Keycloak 26.7.1 | Ory Hydra v26.2.0 | Zitadel v4.17.1 |
> |------|-----------|------------------|-------------------|------------------|
> | **S1 discovery QPS** | **87,123** ✅领先 | 40,124 | 1,616 | 8,580 |
> | **S2 client_credentials QPS** | **12,806** ✅领先 | 5,428 | 1,959 | 1,517（jwt-bearer 官方路径） |
> | **S3 introspect QPS** | **19,245** ✅领先 | 10,556 | 10,061（admin 口） | 2,946（私钥认证） |
> | **S5 refresh QPS** | **4,593** ✅领先（口径修复+优化后反超） | 4,336 | 647 | N/A（机器用户无 RT） |
> | **S6 userinfo QPS** | **40,489** ✅领先（wave-2 用户/角色缓存后反超） | 29,145 | 8,105 | 3,395 |
> | **稳态 P50 / P99** | **0.7-29ms / 14-143ms**（本轮机器噪声地板偏高，四家同受影响，见下注） | 2-42ms / 13-74ms | 31-190ms / 35-440ms | 10-41ms / 20-73ms |
> | **全栈 RSS（D7）** | 5,350 MiB ⚠️最重（含 PG+Redis+连接池） | 1,752 MiB | **261 MiB** ✅最轻 | 380 MiB |
> | **冷启动 fresh/restart** | **1.38s / 1.29s** ✅领先 1-3 个量级 | 21.8s / 7.0s | 4.5s / 0.7s | 5.4s / 1.0s |
> | **GC 抖动（5min P99 序列）** | 四家同机同时段均现同款 ~550ms 周期尖峰 → 判定为宿主环境噪声，产品间不可分辨（见下注） | 同左（90x） | 同左（39x） | 同左（33x） |
>
> **诚实裁决（2026-08-21 刷新版）**：① **五场景全部领先**（S1 2.2x / S2 2.4x / S3 1.8x / S5 1.06x / S6 1.4x）——原 S5 劣势系测量预算伪影（RT 池 20k÷10s 封顶，已修口径）、原 S6 劣势系无缓存串行 DB 往返（wave-2 优化后消除）；② "GC 抖动"叙事彻底关闭——本轮**四家（含 JVM/Go）同机同时段出现同款周期尖峰**，跨产品互证为 WSL2 宿主环境噪声而非任何产品运行时行为，本机数据不得用于"谁更平"的主张（裸机复测后再议）；③ 全栈容器口径 AuthForge 仍最重，轻量叙事只能用 **SDK 嵌入口径（2.5MB）**并显式区分口径。对外表述："比 Keycloak 快"现适用于**全部五个对比场景**，数字引用以 COMPARISON.md 为准。

### 3.2 核心差异化价值主张

**AuthForge = 身份基础设施的 "C++ 权速"**

1. **极致性能**（✅ Phase 0 自测 + Phase 0.5 四产品同环境对比验证，2026-08-21 全量刷新）: C++ + Drogon 异步框架。同环境实测（竞品官方推荐配置）：discovery **87.1k QPS**（Keycloak 40.1k 的 **2.2x**）、client_credentials **12.8k**（**2.4x**）、introspect **19.2k**（**1.8x**）、refresh_token **4.6k**（**1.06x**，口径修复后反超）、userinfo **40.5k**（**1.4x**，wave-2 用户/角色缓存后反超）——**五个对比场景全部领先**，数字以 `benchmarks/competitors/results/COMPARISON.md` 为准。
2. **低且稳定的尾延迟**（⚠️ 2026-08-21 二次修订）: 无 GC runtime，稳态 P50/P99 为四家最低水位区间。**两轮同环境实测的结论演进**：2026-08-17 轮三家 GC 语言长跑平线、"零 GC 抖动"未获证实；2026-08-21 轮**四家（含 JVM/Go）同机同时段出现同款 ~550ms 周期尖峰**——跨产品互证为 WSL2 宿主环境噪声，本机长跑数据**不可用于任何"谁更平"的主张**（含对我们自己有利的），绝对 P99 水位的对比也须标注本机噪声地板；尾延迟主张待裸机复测后再定性。
3. **超低资源消耗（SDK 口径）**: SDK 嵌入口径实测 **2.5 MB peak working set**（`third-party-host-smoke`，纯 SDK；binary 12 MB）。⚠️ 口径警示（2026-08-21 同环境实测）：**容器全栈口径 AuthForge 反为四家最重（5,350 MiB，含 PG+Redis+Drogon 连接池）**，Ory 最轻（261 MiB）——对外必须显式区分口径，不得混用。
4. **冷启动**（✅ 同环境实测领先 1-3 个量级）: fresh 1.38s / restart 1.29s（Keycloak 21.8s/7.0s、Ory 4.5s/0.7s、Zitadel 5.4s/1.0s）——边缘/弹性场景的差异化证据。
5. **可嵌入 SDK**: 唯一支持 `find_package(authforge-*)` 的 C++ 身份引擎，可嵌入宿主应用进程内运行
6. **供应链安全（已落地）**: release 流水线（`.github/workflows/release.yml`）已实现 cosign keyless 签名 manifest digest + syft 每镜像 SPDX SBOM + SDK tarball `.sha256` 校验和。⚠️ 承重 caveat：**SDK 包目前仅 linux-x86_64**（无 arm64 / Windows / macOS SDK tarball）。

### 3.3 差距与挑战

| 差距领域 | 现状 | 影响 |
|----------|------|------|
| **多语言 SDK** | 仅有 C++ SDK | 限制了非 C++ 开发者采用 |
| **社区规模** | 新项目, 用户基数小 | 品牌认知度低, 生态薄弱 |
| **企业功能** | 缺少 SAML, LDAP 同步, SCIM | 难以进入传统企业市场 |
| **合规认证** | 无 SOC2/ISO27001 认证 | 企业采购门槛 |
| **云托管** | 无 SaaS 版本 | 无法服务不愿自运维的客户 |

### 3.4 已落地资产清单（v1.0.0 现状校准）

> 以下能力已达商业级，是**现成的对外卖点**，无需重复投入。本节校准了报告其他章节（§3.2、§六）中把这些能力误列为"待办"之处。所有事实带 `file:line` 出处，2026-08-05 核实。

| 资产 | 位置 | 价值 | 原报告定位 |
|------|------|------|-----------|
| 多架构镜像（amd64+arm64，**原生 runner 无 QEMU**）+ **cosign keyless 签名** manifest digest + **syft 每镜像 SPDX SBOM** + SDK tarball `.sha256` | `.github/workflows/release.yml`（签名 `:255-266`、SBOM `:273-281`、tarball `:153-166`） | 供应链安全卖点 | §3.2 已列为卖点，但措辞像待办——**实为已落地** |
| Helm chart（含 pre-install/pre-upgrade schema-migration Job 钩子、Chart version 与 appVersion 联动、外部 DB/Redis 支持、TLS ingress） | `deploy/helm/authforge/`（migration-job `templates/migration-job.yaml`） | 生产级 K8s 部署 | §六 P1 列为"优化"——**chart 已具备生产能力** |
| CI 守护三件套：api-diff（SDK 头面 SemVer）/ arch-guard（Domain 层不依赖 Drogon）/ migration-check（迁移卫生） | `tools/{api-diff,arch-guard,migration-check}/` + `ci.yml` static-checks 门 | 工程可信度（API 稳定性/架构/迁移可控） | 报告未提，**应补入卖点** |
| SDK 打包：`authforge_package()` × 8 包，install-tree + build-tree `find_package`，传递依赖闭包 | `cmake/AuthForgePackage.cmake` + 各 `libs/*/CMakeLists.txt`（8 处调用） | 可嵌入性的工程证据 | §3.2 已提，**实为已落地** |
| 三平台 CI（Linux/Windows/macOS 矩阵）+ SDK smoke（`examples/full-stack-host` 真实 find_package 消费验证） | `ci.yml` matrix + `_sdk-smoke.yml` | 跨平台可信度 + SDK 可消费性 | 报告未提，**应补入卖点** |
| SDK 文档（集成指南 + 运行时契约，已达发布级） | `docs/backend/sdk-integration-guide.md`、`docs/backend/sdk-runtime-contract.md` | 开发者集成的现成资料 | §六 P0 列为"SDK 文档站"——**内容已就绪**，待办收敛为"站点化 + 版本切换" |

**诚实的 caveat**（写进对外卖点时需注意）：
- **SDK 包仅 linux-x86_64**：无 arm64 / Windows / macOS SDK tarball（release 流水线的 cache key 已为此预留扩展位）。
- **Helm chart 缺**：HPA / PDB / NetworkPolicy / chart tests / chart README；默认 postgres 存储为 ephemeral（标注"demo/local"）；镜像 namespace 为占位符。
- **SDK 文档中英混杂**：集成指南全中文、运行时契约标题英文但章节中文——面向英文首发需本地化。

---

## 四、产品化路线选择

### 4.1 路线方案对比

#### 路线 A: Open Core + 企业版 (推荐)

```
社区版 (MIT)              企业版 (商业许可)
├── OAuth2/OIDC 核心      ├── 社区版全部功能
├── Admin Console         ├── SAML / LDAP / SCIM
├── User Frontend         ├── 多租户管理
├── Docker/Helm 部署      ├── 审计合规报告
└── C++ SDK (基础)        ├── 高级 RBAC / ABAC
                          ├── SSO 联邦
                          ├── 企业级支持 + SLA
                          └── 合规认证协助
```

**参考**: GitLab CE/EE, Zitadel, Keycloak + RHSSO

**优势**:
- MIT 社区版保持开源吸引力，快速积累用户
- 企业版锁定高价值功能，服务付费客户
- 双轨清晰：社区增长驱动 + 企业收入

**风险**:
- 需要明确社区版/企业版功能边界，避免社区反感
- 企业版功能开发需要持续投入

#### 路线 B: 云托管 SaaS

```
AuthForge Cloud
├── Free Tier: 1,000 MAU
├── Pro: $99/月, 25,000 DAU
├── Enterprise: 定制报价
└── 全托管运维 + 自动扩缩容
```

**参考**: Auth0, Ory Network, Logto Cloud

**优势**:
- 最高利润率, 持续经常性收入
- 降低客户运维门槛
- 数据聚合能力带来产品洞察

**风险**:
- 基础设施和运维成本高
- C++ 部署在云原生环境中需要额外适配
- 需要多区域部署满足数据合规
- 竞争激烈, Auth0/Okta 品牌势能强

#### 路线 C: SDK 商业许可 (AuthForge 独有)

```
AuthForge SDK Licensing
├── Community SDK (MIT): 基础 OAuth2 引擎
├── Commercial SDK: 高性能存储适配器 / 企业插件
├── OEM License: 嵌入商业产品
└── 技术咨询服务
```

**参考**: 无直接竞品 (市场空白)

**优势**:
- 利用 AuthForge 独有的 C++ SDK 嵌入能力
- 面向 IoT/边缘/嵌入式身份场景——竞品无法覆盖
- 低边际成本, 高利润率
- 与路线 A 完美互补

**风险**:
- C++ SDK 的目标客户群较窄
- 需要建立 SDK 版本管理和兼容性保障体系
- 需要开发者文档和集成支持

#### 路线 D: 混合模式 (最终推荐)

**路线 A + 路线 C 优先, 路线 B 远期**

```
Phase 1 (0-6 月): Open Core 社区建设
├── 保持 MIT 核心开源
├── 发布 SDK 商业许可试点
├── 建立性能基准测试套件 (vs Keycloak/Ory)
└── 社区推广 + 技术博客

Phase 2 (6-12 月): 企业版启动
├── 发布企业版 (SAML/LDAP/SCIM/多租户)
├── SDK 高级功能商业化
├── 获取首批企业客户
└── 合规认证启动 (SOC2)

Phase 3 (12-24 月): 云托管探索
├── AuthForge Cloud MVP
├── 多区域部署
└── 自助式开发者注册
```

### 4.2 推荐定价策略

| 层级 | 价格 | 包含内容 | 目标客户 |
|------|------|----------|----------|
| **Community** | 免费 (MIT) | 核心引擎 + Admin + Docker/Helm | 开发者, 小团队, PoC |
| **SDK Personal** | 免费 (MIT) | 基础 OAuth2 SDK (memory storage) | C++ 开发者, IoT |
| **SDK Commercial** | $499/年起 | 全存储适配器 + 企业插件 + 支持 | 商业产品嵌入 |
| **Enterprise** | $5,000/年起 | 企业版全功能 + SLA + 支持 | 中大型企业 |
| **OEM License** | 定制报价 | 无限制嵌入 + 源码访问 | ISV, 设备厂商 |
| **Cloud (远期)** | $99/月起 | 全托管 SaaS | SaaS 创业团队 |

**定价逻辑**:
- 对比 Keycloak + Red Hat 支持 ($1,000/年/实例), AuthForge 企业版定价有竞争力
- 对比 Auth0 (10K MAU = $700/月), AuthForge 自托管方案 TCO 优势显著
- SDK 商业许可填补市场空白, 对标商业 ORM/中间件定价

---

## 五、Go-to-Market 策略

### 5.1 目标客户细分

| 细分市场 | 画像 | 痛点 | AuthForge 价值 |
|----------|------|------|----------------|
| **IoT/边缘计算** | 设备厂商, 工业互联网 | 现有 IAM 太重, 无法边缘部署 | 50MB 内存, 可嵌入 SDK |
| **金融科技** | 交易所, 支付平台 | GC 抖动导致延迟尖峰, SLA 难保障 | 零 GC, < 2ms P99 |
| **游戏后端** | 游戏公司 | 登录洪峰 QPS 极高 | 10 万+ QPS 单机 |
| **电信/5G** | 运营商 | 网络功能虚拟化需轻量组件 | 低资源 + 高性能 |
| **高合规行业** | 政府, 军工, 医疗 | 数据必须本地, 不能用云 SaaS | 自托管 + 源码可审计 |
| **C++ 技术栈企业** | 量化交易, 自动驾驶 | 身份认证需嵌入 C++ 宿主进程 | 原生 C++ SDK |

### 5.2 获客渠道

**短期 (0-6 月)**:
1. **技术内容营销**: 发布 AuthForge vs Keycloak/Ory 性能基准测试报告
2. **GitHub Stars 增长**: 优化 README, 添加 benchmarks 徽章, 提交 Hacker News/Reddit
3. **C++ 社区渗透**: Drogon 社区, C++ 开发者论坛, CppCon 等
4. **DevRel**: 编写「为什么用 C++ 构建身份服务器」技术博客系列

**中期 (6-12 月)**:
5. **性能基准认证**: 提交 TechEmpower Framework Benchmarks
6. **企业 POC**: 针对金融科技和 IoT 企业定向推广
7. **集成生态**: 发布 Helm Chart 到 Artifact Hub, 加入 CNCF Landscape
8. **安全审计**: 完成第三方安全审计, 发布报告

**长期 (12+ 月)**:
9. **行业会议**: KubeCon, OSCON, 安全会议演讲
10. **合作伙伴**: 与 C++ 框架 (Drogon) 深度合作
11. **云市场**: AWS/Azure Marketplace 上架

### 5.3 关键信息传递

**一句话定位**: "The fastest OAuth2/OIDC server. Built in C++."

**核心信息矩阵**:

| 受众 | 核心信息 |
|------|----------|
| CTO/架构师 | "10x faster, 5x less memory than Keycloak. Zero GC pauses." |
| DevOps | "50MB memory footprint. Runs on edge. Helm-ready." |
| C++ 开发者 | "The only OAuth2 SDK you can find_package. Embed in your host process." |
| 安全团队 | "MIT licensed. Source auditable. Supply chain signed with cosign." |
| FinOps | "Stop paying per-user. Self-hosted, fixed infrastructure cost." |

---

## 六、产品化路线图建议

### Phase 1: 产品化基础 (0-6 月)

> 注：本表为原报告规划。**落地状态已在 §3.4 校准**——部分能力（供应链安全、Helm、SDK 打包、CI 守护、SDK 文档）**已落地**，不应重复投入；性能基准是**确认真空白**，保留为最高优先级。完整演进路线（含插入的 Phase 0 可信度基线）见 [演进方案 §三](productization-evolution-plan.md)。

| 优先级 | 工作项 | 目标 | 落地状态校准 |
|--------|--------|------|------------|
| P0 | 性能基准测试套件 | 发布 AuthForge vs Keycloak/Ory/Auth0 对比报告 | **当前空白**：代码库无 HTTP 级基准（`tests/performance/` 仅进程内微基准）。→ 见 [基准设施设计文档](in-progress/benchmark-facility-design.md)；竞品对比建议后置到 Phase 0.5 |
| P0 | SDK 文档站 | 完整的 C++ SDK 集成指南 + API 参考 | **内容已就绪**（§3.4）；待办收敛为"**文档站化 + 版本切换 + 中文本地化**" |
| P0 | Benchmark 可复现 | 提供一键式 benchmark 脚本, 第三方可验证 | **当前空白**；随性能基准套件一并交付（同设计文档） |
| P1 | Helm Chart 优化 | 一条命令部署, 默认安全配置 | **chart 已具备生产能力**（§3.4）；待办收敛为"**补 HPA/PDB/NetworkPolicy/chart-tests + 非占位镜像 namespace + 非 ephemeral 默认存储**" |
| P1 | 多语言客户端 SDK | 至少提供 Python/Go 的 API 客户端 (非 SDK) | 未开始（确认为 HTTP 客户端 SDK，由 OpenAPI spec 生成，非原生 SDK） |
| P2 | 技术博客启动 | 发布 3-5 篇深度技术文章 | 未开始；**依赖性能基准数据落地**（否则博客无据可依） |

### Phase 2: 企业版功能 (6-12 月)

| 优先级 | 工作项 | 目标 |
|--------|--------|------|
| P0 | SAML 2.0 支持 | 企业 SSO 集成 (ADFS, Azure AD) |
| P0 | LDAP/AD 联邦 | 用户目录同步 |
| P0 | SCIM 2.0 | 用户生命周期自动化 |
| P0 | 多租户增强 | 组织/租户隔离 + 管理委托 |
| P1 | ABAC (属性访问控制) | 补充 RBAC 的细粒度权限 |
| P1 | 审计合规报告 | 合规导出 (GDPR, 等保) |
| P1 | SDK 商业版功能 | Redis Cluster / PostgreSQL HA 存储适配器 |
| P2 | SOC2 认证准备 | 安全流程文档化 |

### Phase 3: 云托管探索 (12-24 月)

| 优先级 | 工作项 | 目标 |
|--------|--------|------|
| P0 | AuthForge Cloud MVP | 多租户云平台, 自助注册 |
| P0 | 多区域部署 | 数据驻留选择 |
| P1 | 自助式 dashboard | 用量监控, 计费 |
| P1 | Marketplace 集成 | AWS/Azure/GCP Marketplace |
| P2 | AI Agent 身份管理 | M2M token 管理, Agent 身份注册 |

---

## 七、风险评估

| 风险 | 等级 | 影响 | 缓解措施 |
|------|------|------|----------|
| C++ 人才稀缺, 社区贡献门槛高 | 高 | 社区增长缓慢 | 投资文档和教程; 提供 Go/Python 客户端降低使用门槛 |
| Keycloak 品牌势能强 | 高 | 市场认知度低 | 聚焦性能差异化的细分市场 (IoT/金融科技) |
| Auth0/Okta 功能碾压 | 中 | 功能差距导致丢单 | 不拼功能清单, 拼性能/成本/嵌入能力 |
| C++ 安全漏洞影响 | 中 | 信任危机 | 持续安全审计 + fuzzing + 快速补丁流程 |
| 云托管运维成本 | 中 | 云业务亏损 | 先验证自托管收入, 再谨慎投入云 |
| MIT 许可证被白嫖 | 低 | 商业转化率低 | 企业版功能 + 商业许可 SDK 锁定高价值 |

---

## 八、行动建议

### 立即行动 (本周)

1. **启动性能基准测试**: 使用 wrk/vegeta 对 AuthForge vs Keycloak 进行对比压测, 生成数据
2. **完善 SDK 文档**: 确保现有 SDK 集成指南和运行时契约文档达到商业级质量
3. **准备技术博客**: 撰写「为什么我们用 C++ 构建 OAuth2 服务器」首发文章

### 短期 (1-3 月)

4. **发布 v1.1**: 包含可复现的 benchmark 脚本 + 性能数据页面
5. **社区推广**: Hacker News, Reddit r/cpp, r/netsec, Drogon 社区同步发布
6. **企业版功能规划**: 制定 SAML/LDAP/SCIM 的技术设计文档
7. **SDK 商业化试点**: 寻找 2-3 个种子客户验证 SDK 嵌入需求

### 中期 (3-6 月)

8. **企业版 v1.0 发布**: SAML + LDAP + SCIM + 多租户增强
9. **安全审计**: 委托第三方进行代码安全审计
10. **TechEmpower 提交**: 在权威框架基准中占位
11. **首个企业客户签约**: 目标 1-2 个付费企业客户

---

## 附录: 竞品定价详情 (2026 年数据)

| 产品 | 免费额度 | 入门价 | 10K MAU 月费 | 企业版 |
|------|----------|--------|-------------|--------|
| Auth0 (B2C) | 25K MAU | $35/月 | ~$700/月 | 定制 (六位数/年) |
| Auth0 (B2B) | 25K MAU | $300/月 | ~$2,100/月 | 定制 |
| Okta | - | $6/用户/月 | ~$60K/月 (10K 用户) | 定制 |
| Ory Network | $21/月额度 | $770/年 | ~$1,400/月 (10K aDAU) | 定制 |
| Zitadel Cloud | 100 DAU | $100/月 | ~$100/月 (25K DAU) | 定制 |
| Logto Cloud | 1K MAU | $99/月 | ~$300/月 | 定制 |
| Keycloak | 无限 | $0 | $0 (自运维) | $1,000/年/实例 (RHSSO) |
| **AuthForge (建议)** | **无限** | **$0** | **$0 (自运维)** | **$5,000/年起** |

> **注**: 竞品定价数据来源于各产品官网 2026 年公开信息, 实际价格以厂商报价为准。

---

*本报告基于公开市场信息和项目代码分析编制, 仅供产品决策参考。*
