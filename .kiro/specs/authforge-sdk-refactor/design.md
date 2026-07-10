# AuthForge 重构设计文档：产品为主 + 协议引擎 SDK 化

> 版本：v1.0（设计草案） · 目标产物版本：v1.0.0（重构后重置）
> 范围：将现有全栈 OAuth2/OIDC IdP 重构为「可生产交付的产品」+「可独立复用的 SDK 组」双形态。

---

## Overview

本文档定义将现有全栈 OAuth2/OIDC IdP 重构为「可生产交付的产品」+「可独立复用的 SDK 组」双形态的架构设计。以下小节涵盖目标/非目标、路线决策与领域模型评估。

### 目标与非目标

### 1.1 目标

| 目标 | 可度量验收标准 |
|------|--------------|
| 协议引擎可独立复用 | 第三方 Drogon 应用仅 `find_package(authforge-oauth2)` + 实现 3 个端口即可跑通授权码流（由 `examples/third-party-host` 冒烟证明） |
| 身份能力可独立复用 | `find_package(authforge-identity)` 可单独用作登录/用户/MFA 系统，不引入 oauth2 包 |
| 产品可生产交付 | 一条命令产出多架构镜像 + Helm chart；带版本化迁移与回滚 |
| 边界清晰且不腐化 | 每包独立 CMake target + 独立测试 + 显式对外头文件；架构守卫工具在 CI 强制依赖方向 |
| 跨平台可复现 | 三平台统一 Conan 解析依赖（含 Drogon + OpenSSL 3.5 LTS），取消源码编译 Drogon |
| 不破坏现有能力 | 每个里程碑结束 CI 全绿；现有测试（`DROGON_TEST` 宏，`unit/` 139、后端总 319）+ Admin API ~52 + OAuth2 ~55 + E2E 作为回归安全网 |

### 1.2 非目标（本次明确不做）

- 多租户强隔离（schema-per-tenant / RLS / 分片）——属未来「服务/SaaS」路线，本次仅预留 `TenantId` 维度缝。
- 计费、租户自助开通、控制平面等 SaaS 能力。
- 事件溯源 / CQRS / 领域事件总线（见 §3 DDD 评估，明确排除）。
- 重写。策略是「接口解耦 + 渐进搬移」，用现有测试护航。

### 1.3 硬约束（沿用项目铁律）

- ORM 模型不可手改，需 `drogon_ctl` 重新生成。
- 禁用协程（`CoroMapper`）；异步用回调，捕获 `sharedCb`。
- 无 emoji；C++17；Google C++ Style；100 列。
- `git push` 需人工复核。

---

## 2. 路线决策（已确认）

**以「产品」为主线交付，同时沉淀「协议引擎 + 身份」两个可嵌入 SDK 作为第二类交付物。「服务(SaaS)」作为产品成熟后的商业延伸，不在本次范围。**

产品与 SDK 共享同一份重构收益：为让协议引擎可复用而做的解耦，同时提升产品自身的可维护性，避免二选一。

---

## 3. 领域模型（DDD）评估结论

**采用「战略 DDD（限界上下文）+ 轻量战术 DDD（值对象 + 聚合 + 仓储）」，排除重型机制。**

### 3.1 采用的部分

- **战略 DDD（限界上下文）**：用于定义包/模块边界，直接解决「`IOAuth2Storage` 上帝接口」问题。
- **值对象（Value Object）**：`Scope` / `Subject`（如 `local:alice`）/ `TokenValue` / `ClientId` / `RedirectUri` / `PkceChallenge` / `TenantId`。消除裸 `std::string` 引发的校验/注入类缺陷。
- **聚合（Aggregate）**：`AuthorizationGrant`（授权码 + PKCE + consent 上下文，对应现有 `AuthorizationTransaction`）/ `Client` / `TokenPair`。聚合边界即事务边界（对应现有 `saveTokenPair` / `revokeTokenFamily`）。
- **仓储（Repository）**：按聚合拆分的 per-aggregate 仓储接口（见 §7）。

### 3.2 排除的部分（避免过度设计）

- 事件溯源 / CQRS：与 Drogon ORM + 异步回调范式冲突，收益不抵成本。审计用现有 `AuditLogger` 轻量替代。
- 完整领域事件总线：无跨上下文事件驱动需求，同步/回调足够。

### 3.3 限界上下文 → 包归属

| 限界上下文 | 职责 | 归属包 |
|-----------|------|--------|
| Protocol（授权与令牌） | authorize/token/introspect/revoke、PKCE、授权事务 | `oauth2` |
| ClientRegistry（客户端） | client 注册/密钥/redirect/scope 配置 | `oauth2` |
| AccessControl（授权决策） | **consent + scope 策略 + 决策引擎 → `oauth2`**；**RBAC 数据（roles/permissions/user-role）→ `identity`**（见 §5.3） | 分摊 |
| Identity（身份） | user/凭证/MFA/WebAuthn/社交登录/会话 | `identity` |
| Tenancy（租户） | `TenantId` 值对象 → `common`；Organization 管理 → 产品层 | common + 产品 |
| Observability（可观测） | 审计/metrics 模型 → `common`；导出器 → 适配器 | common + 适配器 |

---

## Architecture

目标架构分层与依赖方向如下。

```
┌───────────────────────────────────────────────────────────────┐
│ Apps（可执行 / 可交付物）                                         │
│   apps/server（产品：装配 oauth2 + identity + 存储 + drogon）     │
│   examples/third-party-host（SDK 消费者样例/冒烟）                │
└───────────────┬───────────────────────────┬───────────────────┘
                │ links + 装配注入            │ find_package
┌───────────────▼───────────────┐  ┌──────────▼──────────────────┐
│ Adapter 层（允许依赖 Drogon）   │  │ SDK 包（对外交付）            │
│   libs/drogon（插件/ctl/filter）│  │   authforge-oauth2           │
│   libs/storage-*（PG/Redis/Mem）│  │   authforge-identity         │
│   libs/observability-prometheus │  │   authforge-common           │
└───────────────┬───────────────┘  └──────────┬──────────────────┘
                │ implements 接口               │
┌───────────────▼───────────────────────────────▼───────────────┐
│ Domain 层（禁止依赖 Drogon；允许 jsoncpp）                        │
│   libs/oauth2   · libs/identity   · libs/common                 │
│   值对象/聚合 · Repository 接口(纯虚) · Ports(端口接口)          │
└───────────────────────────────────────────────────────────────┘
```

### 4.1 依赖方向铁律

1. Domain 层（`common`/`oauth2`/`identity`）**禁止** `#include <drogon/...>`；**允许**依赖 jsoncpp（`Json::Value`）。
2. `oauth2 → common`，`identity → common`；**`oauth2` 与 `identity` 互不编译依赖**（关键决策，见 §5）。
3. Adapter 层（`storage-*`/`drogon`/`observability-prometheus`）实现 Domain 层的纯虚接口，允许依赖 Drogon。
4. 产品 `apps/server` 依赖所有层，负责装配（把 identity 的实现注入 oauth2 的端口）。
5. 由 `tools/arch-guard` 在 CI 强制上述规则。

---

## Components and Interfaces

本节定义包划分、端口解耦、存储接口拆分与 Core 去 Drogon 化——即各组件与其对外接口。

### 5. 包划分与端口解耦（核心设计）

### 5.1 三个 Domain 包

| 包 | 命名空间 | find_package 名 | 目录 | 职责 |
|----|---------|----------------|------|------|
| 共享内核 | `authforge::common` | `authforge-common` | `libs/common` | Result/错误目录/值对象(Subject,Scope,TenantId)/端口基类/Observability 模型(AuditEvent,IMetrics) |
| OAuth2 SDK | `authforge::oauth2` | `authforge-oauth2` | `libs/oauth2` | 协议 + client registry + 授权决策(consent+scope 策略) + **identity 端口(接口)** |
| Identity SDK | `authforge::identity` | `authforge-identity` | `libs/identity` | user/凭证/MFA/WebAuthn/社交/会话 + RBAC 数据存储 |

> 目录名不带 `authforge-` 前缀（简洁）；命名空间与 find_package 名带 `authforge` 品牌前缀（对外一致）。

### 5.2 端口解耦：产品层装配注入（已确认方案）

`oauth2` 在授权决策时需要「角色、subject 解析、userinfo」，但**不得编译依赖 `identity`**。做法：

- `oauth2` 在 `libs/oauth2/include/authforge/oauth2/ports/` 定义端口接口：
  - `ISubjectResolver`：subject（`local:alice`）→ 内部 userId。
  - `IRoleProvider`：userId → 角色列表（供 scope 分层校验）。
  - `IUserInfoProvider`：userId → OIDC userinfo claims。
- `identity` 在其实现中提供这些端口的具体实现类（如 `identity::IdentityRoleProvider`），但**通过实现 `oauth2` 的端口头来完成**——为避免 `identity` 编译依赖 `oauth2`，端口接口的「最小契约」采用以下二选一（落地时定，倾向 A）：
  - **方案 A（推荐，纯装配）**：端口接口下沉到 `common`（`authforge::common::ports`）。`oauth2` 和 `identity` 都只依赖 `common`。产品层 `apps/server` 构造 `identity` 的实现，注入到 `oauth2` 的服务构造函数。二者零直接耦合。
  - 方案 B（次选）：端口留在 `oauth2`，`identity` 依赖 `oauth2` 的仅头端口——但这违背「互不依赖」，放弃。

> **结论：端口接口放在 `common`。** 这样「`oauth2` 与 `identity` 互不编译依赖」严格成立，产品层做唯一装配点。
>
> **边界类型约束（评审 B7）**：跨端口边界的参数/返回类型**必须全部是 `common` 类型**（值对象/DTO），**不得**是 `identity` 或 `oauth2` 私有类型——否则「互不依赖」在编译期即破裂。arch-guard 应一并检查端口头只 include `common`。

### 5.3 AccessControl 的责任分摊

| AccessControl 组成 | 本质 | 归属 |
|-------------------|------|------|
| Consent（用户对 client+scope 授权） | OAuth2 专属 | `oauth2`（`IConsentRepository` + 决策） |
| Scope 模型 + scope 分层策略（"scope X 需 admin"） | 保护协议 scope | `oauth2` |
| 授权决策引擎 | 协议资源访问闸门 | `oauth2` |
| RBAC 数据（roles/permissions/user-role） | 描述用户身份与授予 | `identity`（实现 `common::ports::IRoleProvider`） |

不单独设立 AccessControl 包：其两半 owner 不同，独立会产生模糊边界与循环依赖诱惑。

### 5.4 Adapter / 产品包

| 包 | 命名空间 | 目录 | 依赖 Drogon | 职责 |
|----|---------|------|:-----------:|------|
| Postgres 存储 | `authforge::storage::postgres` | `libs/storage-postgres` | 是 | 实现 oauth2/identity 的仓储接口；**ORM 模型（`Oauth2*`/`Users`/`Roles` 等）归此包**（`drogon::orm` 类型，不得进 Domain，见 §5.5/F2） |
| Redis 存储 | `authforge::storage::redis` | `libs/storage-redis` | 是 | 缓存/临时数据仓储实现 |
| 内存存储 | `authforge::storage::memory` | `libs/storage-memory` | 否* | 测试/无外部依赖部署 |
| Prometheus 导出 | `authforge::observability::prometheus` | `libs/observability-prometheus` | 是 | 实现 `common::IMetrics`；**可选新增——当前 4 份 config 均用原生 `drogon::plugin::PromExporter`，无自研导出器，无「双轨」冲突；仅在需脱 Drogon metrics 时才做（评审 H7/B3，见 §15）** |
| Drogon 绑定 | `authforge::drogon` | `libs/drogon` | 是 | Drogon 插件（DI 装配器）、controller、filter、view；**含 Drogon 自注册符号，消费者链接须 whole-archive，见 §5.5/F1** |
| 产品服务 | （app，无命名空间导出） | `apps/server` | 是 | main + bootstrap 装配 + Organization 管理 + 迁移 |

> *内存存储可不直接依赖 Drogon，若需 Drogon 工具则归 Adapter 层。

### 5.5 Drogon 自动注册与链接策略（关键，评审 F1 → **已用 `AutoCreation=false` 方案验证解决，取代原 whole-archive 方案**）

> **本节内容为原设计（whole-archive 方案）的历史记录 + 实施后发现的更优方案，两者一并保留供对照。M3 Task 20/23 实施时采用的是下方"已验证方案"，whole-archive **未被使用**，Task 22 因此判定为大概率不再需要（见文末结论）。**

**原设计假设**：Drogon 的 controller/filter/**视图类**/**插件类**均通过静态初始化自注册（`ADD_METHOD_TO`、`drogon_create_views`、以及插件按类名字符串反射等在 TU 级构造/查找全局对象）。当前 `OAuth2Plugin` 是 CMake **OBJECT 库**，强制所有目标文件进入最终可执行体，**恰好规避**了「未被显式引用的注册符号被链接器丢弃」的问题。改为「静态库 + `find_package`」后，注册符号可能被链接器当作未引用而丢弃，导致 **controller/filter 静默不注册、路由 404**——原设计给出的应对是对含注册符号的库整体做 whole-archive 链接。

**已验证的更优方案（用户提出，实测确认，见 PROGRESS.md "AutoCreation=false" 相关章节）**：

Drogon 的 `HttpController<T, AutoCreation>` / `HttpFilter<T, AutoCreation>` 模板第二参数（默认 `true`）**只控制是否自动挂路由**（`methodRegistrator` 构造体里 `if (AutoCreation) T::initPathRouting();`），**不影响** `DrObject<T>` 层面的类型注册（`DrClassMap::registerClass`，只要类满足 `is_default_constructible` 就会注册，与 `AutoCreation` 无关）。`HttpAppFramework::registerController<T>(shared_ptr<T>)`/`registerFilter<T>` 提供了 `AutoCreation=false` 类的显式注册入口（`static_assert(!T::isAutoCreation, ...)`），调用体是 `DrClassMap::setSingleInstance(ctrlPtr); T::initPathRouting();`——即"显式提供实例 + 显式触发路由挂载"。

**核心结论**：把 controller 声明为 `HttpController<T, false>`，在装配代码（`main.cc`/未来 `apps/server` bootstrap）里显式 `drogon::app().registerController(std::make_shared<T>())`，路由挂载从"隐式静态初始化副作用（链接器可能因未被引用而整体丢弃该 `.o`）"变成"真实的、编译器/链接器可见的函数调用链"——**因为问题根源（隐式静态初始化、无显式引用）被消除了，controller 迁入静态库后完全不需要 whole-archive**，普通 `target_link_libraries` 即可。

已用真实运行的可执行文件 + curl 发起真实 HTTP 请求验证（不是仅靠单测断言）：`libs/drogon` 用普通链接（非 whole-archive），`main.cc`/`test_main.cc` 显式 `registerController` 全部 15+1 个 controller 后，`/health`、`/login`、`/api/admin/dashboard`、`/oauth2/authorize`、`/.well-known/openid-configuration` 等路由全部可达，`ctest` 226/288 全绿（视里程碑推进情况递增），零回归。

**这条结论的适用范围与例外**：

- **Controller**（本节新方案主要覆盖对象）：15+1 个 controller 全部改为 `AutoCreation=false` + 显式 `registerController`，已完成迁移，全程不用 whole-archive。
- **Filter（`ADD_METHOD_TO(..., "命名空间::FilterClassName")` 字符串引用的被动过滤器）**：`AuthorizationFilter`/`OAuth2AuthFilter` 保持 `AutoCreation=true`（默认）不变——它们是被 controller 按名字符串通过 `DrClassMap` 动态查找实例化的，不是"自己主动挂路由"模式，这条路径本来就没有 whole-archive 问题（`ADD_METHOD_TO` 字符串本身就是一个真实的、编译期确认过的引用意图）。**目前仍是** `OAuth2Plugin/include/oauth2/filters/` 下的旧命名空间实现（`oauth2::filters::*`）在被路由字符串实际引用；`libs/drogon` 里 Task 20 slice 2 迁移的同名副本未被任何 `ADD_METHOD_TO` 字符串引用，是路由意义上的死代码（保留供未来统一命名空间时切换调用点，不阻塞）。
- **插件类**（`OAuth2Plugin`）：**不适用**此方案——`drogon::Plugin<T>` 没有 `AutoCreation` 模板参数，插件的注册/查找（`getPlugin<T>()`）是纯 `DrClassMap` 机制，Drogon 内部按 config 里的类名字符串反射构造，没有"手动注册"这个口子。**§5.7 的插件按名反射风险分析对插件仍然完全成立，不受本节修正影响**。
- **视图类**（`login.csp`/`consent.csp`）：未受本节方案覆盖，但已验证 `SessionController::showLoginPage`（已用 `AutoCreation=false` 迁移）调用 `HttpResponse::newHttpViewResponse("login", data)` 能正常工作——说明 view 渲染不依赖 controller 所在的库形态，视图文件可以继续留在原位（`OAuth2Server/views/`，未随 controller 一起迁入 `libs/drogon`），不阻塞。**视图渲染名跨库保活（评审 H5/B2）**：全仓仅 `SessionController.cc` 渲染 `login`；consent 走重定向到前端 `/consent`，服务端 `consent.csp` 无渲染入口 → 遗留死文件判定不变（见 §5.9）。

**对 Task 22（whole-archive）的影响**：**大概率不再需要**——`AutoCreation=false` 方案已验证完全替代其作用，成本比原方案更低（不需要 CMake `$<LINK_LIBRARY:WHOLE_ARCHIVE,...>`/`-Wl,--whole-archive`/`/WHOLEARCHIVE:` 任何形式的特殊链接配置）。仍需评估的是**视图类**和**插件类本身**是否有类似的免 whole-archive 路径，或者这两类天生需要不同的处理方式——插件类已确认没有（见上），视图类因为不是自注册类型（渲染是运行期按名查找模板文件，不是链接期符号），大概率也不需要。Task 22 在 tasks.md 中标注为"评估性任务"，而非"必须实施"。

- 纯 Domain 库（`common`/`oauth2`/`identity`）无自注册符号，正常静态库即可（不受本节讨论影响，原设计对这部分的结论不变）。
- M4 测试从 GLOB 直编改为链接库时，若沿用 `AutoCreation=false` 方案（controller 已经是这个形态），测试可执行体同样只需显式 `registerController`，不需要 whole-archive；若测试直接调方法而不经路由分发（如 `Integration_P0_SuccessBodyShape_GoldenSnapshot` 用 `make_shared<HealthController>()` 直调），`AutoCreation` 值完全不影响这种用法。
- 回归防线：SDK 冒烟（§Testing Strategy）**实发 HTTP 请求**断言 `/oauth2/*` 路由已注册、`login.csp` 可渲染、config 驱动的插件实例化成功——此验收标准不因链接方案变化而改变。

### 5.6 去 Drogon::utils 的端口清单（评审 F3）

Domain 去 Drogon 化的**主体工作不是 OpenSSL（仅 1 处），而是替换散落各处的 `drogon::utils`**。审计到的依赖点：`CryptoUtils.h`（头文件内联 `base64EncodeUnpadded`/`getSha256`/`secureRandomBytes`/`getUuid`）、`PasswordHasher`、`TotpUtils`、`JwkManager`、`TokenService`、`AuditLogger`、`RequestId`（`getUuid`）。此外 Domain 大量使用 Drogon 的 `LOG_*` 宏。

`common::ports` 端口需覆盖：

| 端口 | 替代的 drogon::utils / 能力 | 默认实现（Adapter） |
|------|---------------------------|-------------------|
| `ICryptoProvider` | `getSha256`、`secureRandomBytes`、base64url、HMAC、PBKDF2、RSA/JWT 签名 | OpenSSL 直接实现（脱 drogon） |
| `IUuidGenerator` | `getUuid` | OpenSSL/标准库实现 |
| `IClock` | `now`（TOTP/过期判断） | `system_clock` 实现 |
| `ILogger` | `LOG_DEBUG/INFO/WARN/ERROR` 宏 | Drogon 日志适配 / 标准实现 |
| `IEmailSender` | libcurl SMTP | libcurl 实现 |

工作量提示：`CryptoUtils.h` 是**头文件内联**且被广泛 include，端口化需把所有调用点改为经注入的 `ICryptoProvider`——这是 M2 的主要成本，须在里程碑中单列（见 tasks M2a）。

### 5.7 插件注册与配置加载迁移（关键，评审 H1）

**现状（已核实）**：`OAuth2Server/main.cc` 中 `OAuth2Plugin` **零命中**——插件及其**全部业务配置块**（`storage_type` / `clients` / `admin_users` / `tokens` …）是 Drogon 按 config 里 `"plugins":[{"name":"OAuth2Plugin", "config":{...}}]` 的**类名字符串反射**加载的。共 4 份 config（`config.{json,dev,ci,prod}.json`）依赖这个类名。（这些 config 还含原生插件 `drogon::plugin::PromExporter`（全部 4 份）、`AccessLogger`（dev）、`Hodor`（prod）——均为 Drogon 原生插件、不受本重构影响；方案 A「4 份 config 零改动」仅针对 `OAuth2Plugin` 块，评审 B4。）

**风险**：插件一旦改名 / 退化为装配器 / 迁入 `libs/drogon` 静态库，(a) 静态库丢弃插件自注册符号 → Drogon 反射「plugin not found」，(b) config 的 `plugins[].name` 失配 → **启动加载阶段即崩**（早于且更致命于 F1 的路由 404），(c) 插件 `config{}` 块是产品配置契约，装配器化后这份 schema 从哪读未定义。

**决策（Task 21 已完成，选定方案 A）**：
- **方案 A（低破坏，已选定并实施）**：保留 `OAuth2Plugin` 类名与 config `plugins` 块，插件内部逐步退化为「薄装配器」（构造 Adapter 实现 → 注入 Domain 服务）；插件本体目前是 CMake **OBJECT 库**（非 STATIC），OBJECT 库的目标文件逐个直接链接进消费者，不存在链接器按需抽取丢弃注册符号的问题——这也是"用 whole-archive 保活插件自注册符号"这条风险在**当前阶段未出现**的原因（真正的风险点是"改为静态库形态分发"时才会浮现，见 §5.5 更新后的结论：插件类没有 `AutoCreation` 参数，不适用免 whole-archive 方案，若插件本体未来确实改为静态库分发，仍需要 whole-archive 或等价方案）。config 4 份**零改动**，兼容性最好。验收标准「config 驱动的插件实例化成功」已被现有测试套件隐式覆盖（30+ 处 `getPlugin<OAuth2Plugin>()` 断言非空指针的测试全绿）。
- **方案 B（未采用）**：`main.cc` 显式构造装配（不走 config plugins 反射），把原 `config{}` 块迁到 `custom_config.oauth2`。需同步改 4 份 config + 文档 + 迁移说明，破坏性更大，已放弃。
- Task 20 slice 1-2（迁移 filters/validation）已隐含验证了方案 A 的方向——全程保留 `OAuth2Plugin` 类名、`#include` 路径和 `getPlugin<OAuth2Plugin>()` 调用点不变。

### 5.8 类/文件命名规范（重构中统一梳理，采纳评审建议 3）

现有命名有歧义：`OAuth2StandardController` / `OAuth2Controller` 难从名字区分职责，`OAuth2Plugin` 既是 DI 容器又是业务门面。借重构 + v1.0.0 重置窗口统一梳理。

**约定**：
- **协议端点控制器**按职责/资源命名，禁用模糊的 "Standard"：`OAuth2StandardController` 拆/更名为 `AuthorizationEndpointController`（/authorize、/login、/consent 跳转）、`TokenEndpointController`（/token、/introspect、/revoke）、`DiscoveryController`（/.well-known、jwks）。
- **Domain 服务**：`XxxService`（`AuthorizationService`/`TokenService`/`ClientService`）。
- **仓储接口**：`IXxxRepository`；**端口**：`IXxx`（`ISubjectResolver` 等）；**实现**按后端前缀：`Postgres|Redis|Memory + XxxRepository`。
- **装配器**：`XxxPlugin` 仅保留「Drogon 装配器」语义（业务已下沉 Domain 服务），与业务服务名分离。
- **文件名 = 主类名**；命名空间 `authforge::{common,oauth2,identity,storage,drogon}`。

**时机与安全**：
- **rename-on-move**：类在 M2b/M3/M5 迁移时顺带用**语义化重命名**更名（一次移动+改名，成本最低）。
- **M8 终检**：Task 45 做全局一致性 sweep + 文档更新。
- **破坏性窗口**：类/头重命名是对外 API 破坏性变更，**必须在 v1.0.0 冻结前完成**（现处 pre-1.0，正当其时）；api-diff 基线（Task 34）在命名稳定后再建。

### 5.9 已知遗留问题（评审发现，重构中标记/清理）

- **`consent.csp` 死文件（评审 B2 深挖）**：consent 流程实为**重定向到前端 `/consent`**（`OAuth2StandardController.cc:1683`，可配 `oauth2.consent_url`），服务端 `consent.csp` 无任何 `newHttpViewResponse` 渲染入口——判定为历史遗留死文件。行动：作为独立 issue 跟踪；M5/M8 清理前确认无隐藏引用后删除 `consent.csp` 及其 view 生成；若未来需服务端 consent 页，按 `login` 模式补。不阻塞主线。

---

## 6. 目录结构（重组后，目录名无 authforge- 前缀）

```
authforge/                          # umbrella monorepo（repo 名不变）
├── CMakeLists.txt                  # 顶层：聚合 + 版本(1.0.0) + 选项
├── CMakePresets.json               # 入库（取代 build/ 内生成）
├── conanfile.py                    # 统一依赖：drogon 1.9.13 / openssl 3.5 / ...
├── cmake/                          # Version / Compatibility / Sanitizers / Packaging / ArchGuard
│
├── libs/
│   ├── common/                     # authforge::common  [禁 drogon, 允许 jsoncpp]
│   │   ├── include/authforge/common/
│   │   │   ├── result/             # Result<T,Error>
│   │   │   ├── error/              # ErrorCatalog / ErrorEnvelope（框架无关）
│   │   │   ├── model/              # 值对象: Subject, Scope, ClientId, TenantId, TokenValue...
│   │   │   ├── ports/              # ISubjectResolver, IRoleProvider, IUserInfoProvider, IClock, ICryptoProvider, IUuidGenerator, IEmailSender, ILogger, IMetrics
│   │   │   └── observability/      # AuditEvent 模型
│   │   ├── src/
│   │   └── test/                   # 纯单元（无 DB/无 drogon）
│   │
│   ├── oauth2/                     # authforge::oauth2  [禁 drogon]
│   │   ├── include/authforge/oauth2/
│   │   │   ├── protocol/           # AuthorizationService, TokenService（纯逻辑）
│   │   │   ├── client/             # ClientService
│   │   │   ├── access/             # consent + scope 分层策略 + 决策引擎
│   │   │   ├── pkce/  jwk/         # PKCE、JWK 签名（经 ICryptoProvider 端口）
│   │   │   ├── model/              # AuthorizationGrant, TokenPair 聚合
│   │   │   └── repository/         # IClientRepository, IGrantRepository, ITokenRepository, IConsentRepository
│   │   ├── src/
│   │   └── test/
│   │
│   ├── identity/                   # authforge::identity  [禁 drogon]
│   │   ├── include/authforge/identity/
│   │   │   ├── user/  credentials/ # 用户 + 密码（PasswordHasher 经端口）
│   │   │   ├── mfa/  webauthn/     # TOTP、FIDO2
│   │   │   ├── social/  session/   # Google/WeChat、会话
│   │   │   ├── rbac/               # 角色/权限（实现 common::ports::IRoleProvider）
│   │   │   └── repository/         # IUserRepository, ICredentialRepository, IRoleRepository...
│   │   ├── src/
│   │   └── test/
│   │
│   ├── storage-postgres/           # authforge::storage::postgres  [drogon ORM]（含 models/ ORM 模型 + 仓储实现 + DTO 映射）
│   ├── storage-redis/
│   ├── storage-memory/
│   ├── observability-prometheus/
│   └── drogon/                     # authforge::drogon  [drogon 绑定]
│       └── include/authforge/drogon/  # 插件(装配器)/controllers/filters/views
│
├── apps/
│   └── server/                     # 产品可执行（原 OAuth2Server 瘦身版）
│       ├── src/main.cc             # 仅装配：读配置 → 构造实现 → 注入端口 → run
│       ├── src/bootstrap/          # CorsSetup/SecurityHeaders/ExceptionHandler/OpenApiSetup/MigrationRunner
│       ├── src/organization/       # Organization 管理（产品级，非 SDK）
│       ├── config/                 # config.{dev,ci,prod}.json
│       └── migrations/             # 版本化 SQL（原 sql/）
│
├── frontends/
│   ├── admin/                      # 原 OAuth2Admin
│   └── user/                       # 原 OAuth2Frontend
│
├── tests/
│   ├── contract/                   # 仓储/端口契约测试（三实现共用一套）
│   ├── integration/                # 跨模块 + DB/Redis
│   ├── e2e-backend/                # 协议流 E2E（ctest）
│   └── e2e-frontend/               # Playwright
│
├── examples/
│   └── third-party-host/           # 最小第三方 Drogon 宿主（find_package 集成 + 冒烟）
│
├── tools/                          # 自动化（arch-guard / api-diff / naming / manage-parity / migration-check）
├── deploy/                         # Docker / Helm / Compose
└── docs/
```

命名空间同步：`oauth2::` → `authforge::oauth2::`；`common::error`/`common::config` → `authforge::common::`。

---

## 7. 存储接口拆分（P0 地基）

将现有 **30 个方法**（28 纯虚 + 2 带默认实现，评审 A3 更正，原文「~40」偏高）的 `IOAuth2Storage` 上帝接口按聚合拆成小接口。

### 7.1 拆分映射

| 新接口 | 归属包 | 承接的原方法 | SDK 消费者必须实现？ |
|--------|--------|-------------|:------------------:|
| `IClientRepository` | oauth2 | getClient / validateClient | 是 |
| `IGrantRepository` | oauth2 | saveAuthCode / getAuthCode / markAuthCodeUsed / consumeAuthCode / 授权事务(save/get/delete/markConsumed) | 是 |
| `ITokenRepository` | oauth2 | save/get access+refresh、saveTokenPair、revoke、atomicRevoke、revokeTokenFamily、introspect、incrementIntrospectCount、revokeAccessToken | 是 |
| `IConsentRepository` | oauth2 | hasUserConsent / saveUserConsent / revokeUserConsent | 可选（不启用 consent 则不实现） |
| `IUserRepository` / `IRoleRepository` / `ISubjectMappingRepository` | identity | getUserInfo(×2) / getUserRoles(×2) / getInternalUserId / createSubjectMapping / createUserForExternalLogin | 可选（不集成身份则不实现） |

> **完整性要求（评审 A3）**：Task 7 必须产出「30 个方法 → 目标仓储」的完整映射表，确保迁移零丢失。特别注意易漏项：内省组（`introspectToken`/`incrementIntrospectCount`）与撤销（`revokeAccessToken`）→ `ITokenRepository`；授权事务组（`saveAuthorizationTransaction`/`getAuthorizationTransaction`/`deleteAuthorizationTransaction`/`markTransactionConsumed`）→ `IGrantRepository`；subject-mapping（`getInternalUserId`/`createSubjectMapping`/`createUserForExternalLogin`）→ `ISubjectMappingRepository`。**`deleteExpiredData` 原设计未分配**——归为跨仓储的维护操作：由各仓储各自实现 `purgeExpired()`，产品侧 `CleanupService` 编排调用（不放单一仓储）。

### 7.2 关键保留

- `saveTokenPair` / `revokeTokenFamily` 的**事务原子性**由 `ITokenRepository` 契约声明：默认实现顺序执行，Postgres 覆写为 DB 事务（保留现有正确设计）。
- `consumeAuthCode` 必须校验 `redirect_uri` 匹配（RFC 6749 §4.1.3），保留现有语义。
- 现有 `PostgresOAuth2Storage`（1743 行）拆为多个实现文件，各实现一个仓储接口；`PostgresRepositoryBundle` 聚合供产品用。ORM 模型归 `storage-postgres`（见 §5.5/F2），仓储实现负责 ORM ↔ Domain DTO/值对象映射。
- **consent 端口去内部键耦合（评审 F4）**：`IConsentRepository` / 端口对外用抽象 `UserRef`（由 `ISubjectResolver` 解析），不直接暴露 `internalUserId`（那是 identity 的内部键），避免 oauth2 决策层与 identity 存储细节耦合。

### 7.3 契约测试（分档，评审 F5）

「三实现完全一致」不现实（Memory 无真事务/CAS）。契约测试分两档：

- **功能契约（所有实现必须通过）**：CRUD 语义、单次读写正确性、`consumeAuthCode` 的 redirect_uri 校验与单次消费、过期判断等与并发无关的行为。
- **原子性/事务契约（仅声明支持的实现运行）**：`saveTokenPair` 原子性、`atomicRevokeRefreshToken` 的 CAS、`revokeTokenFamily` 级联、并发下的单次消费。实现通过能力标志（如 `supportsTransactions()`）声明是否参与；Memory 用进程内锁尽力而为并标注局限，Postgres 必须通过全部。
- 这同时验证「SDK 可被外部实现替换」，且明确不同后端的语义边界。

### 7.4 缓存装饰器（CachedOAuth2Storage）再架构（评审 H3）

现有 `CachedOAuth2Storage` 是包裹**整个 `IOAuth2Storage`** 的装饰器（`postgres → CachedOAuth2Storage(Postgres + Redis L2)`）。上帝接口拆成 4+3 个 per-aggregate 仓储后，这个「包一整个接口」的装饰器无直接落点，须重新架构：

- **方案（推荐）**：改为 **per-repository 缓存装饰**——只对读多写少、可安全缓存的仓储（如 `IClientRepository`、只读 userinfo）套 `CachedClientRepository` 等；令牌/授权码这类强一致数据不缓存或仅缓存否定结果。避免「缓存整个接口」带来的失效复杂度。
- 备选：`CachedRepositoryBundle` 包裹一组仓储，保持粗粒度，但失效策略更难。
- **并发安全模式须保留（评审 A1 更正）**：`CachedOAuth2Storage` **已在 HEAD 继承 `std::enable_shared_from_this`**（`CachedOAuth2Storage.h:26-27`，提交 `30a1d1e` 修 defect 1.8；每个异步延续捕获 `self = shared_from_this()`），`OAuth2Plugin.cc` 的 `storage_.reset()` 已安全。**这不是待修缺陷**。迁移时须把这套「`enable_shared_from_this` + `self` 捕获」模式**原样保留**到新的 per-repository 装饰器，`CategoryC_CachedStorageUafTest` 作为**回归门控**（防止拆分时丢失该模式），而非复现现存 bug。
- 落点：`libs/storage-redis`（或独立 `storage-cache`），实现 Domain 仓储接口，对上层透明。

---

## 8. Core 去 Drogon 化（#7 已确认：允许 jsoncpp）

- Domain 层可用 `Json::Value`（jsoncpp 是 C++ 通用库），**禁止** `#include <drogon/...>`、`HttpRequestPtr`、Drogon `EventLoop`、Drogon ORM 类型出现在接口签名中。
- 协议逻辑从 `OAuth2Plugin`（Drogon 插件类）剥离到 `oauth2::protocol` 的纯服务类：
  - 静态工具 `validatePkceCodeVerifier` / `generateSha256Hash` → `oauth2::pkce`（纯函数）。
  - 业务方法 exchangeCode / refresh / introspect / scope 校验 → `AuthorizationService` / `TokenService`。
- 基础设施经端口访问：`ICryptoProvider` / `IUuidGenerator` / `IClock` / `IEmailSender` / `IJwkProvider` / `ILogger`（放 `common::ports`），实现放 Adapter 层。让 Domain 可用假时钟/假 crypto 做确定性单测。
- **去 `drogon::utils` 是本阶段主体工作量**（远大于 OpenSSL 的 1 处迁移）：依赖点清单与端口映射见 §5.6。`CryptoUtils.h` 为头文件内联、被广泛 include，须逐调用点改为注入端口；Domain 内的 `LOG_*` 宏改为 `ILogger`。
- Drogon 插件退化为**装配器**：读配置 → 构造 Adapter 实现 → 注入 Domain 服务 → 注册 controller/filter。

---

## Data Models

### 值对象（Value Object，放 `common::model`）

| 值对象 | 语义 | 校验/不变量 |
|--------|------|------------|
| `Subject` | `provider:localId`（如 `local:alice`、`google:123`） | 非空 provider + 非空 localId；解析/构造对称 |
| `Scope` | 单个 scope 名 | 字符集受限；集合去重 |
| `ClientId` | 客户端标识 | 非空 |
| `RedirectUri` | 回调地址 | 绝对 URI；与登记值精确匹配 |
| `PkceChallenge` | code_challenge + method | method ∈ {plain, S256}；长度 43–128 |
| `TokenValue` | 令牌原值/哈希 | 存储用哈希，禁止日志原值 |
| `TenantId` | 租户维度（预留） | 可空（单租户默认） |

### 聚合（Aggregate，放 `oauth2::model`）

| 聚合 | 组成 | 事务边界 |
|------|------|---------|
| `AuthorizationGrant` | 授权码 + PKCE + consent 上下文（对应现有 `AuthorizationTransaction`） | 单事务 save/consume |
| `TokenPair` | access token + refresh token（+ familyId） | `saveTokenPair` 原子；`revokeTokenFamily` 级联 |
| `Client` | clientId/type/secretHash/redirectUris/allowedScopes | 单实体 |

### DTO 结构（框架无关，放 Domain）

现有 `OAuth2Client` / `OAuth2AuthCode` / `OAuth2AccessToken` / `OAuth2RefreshToken` / `TokenIntrospection`（RFC 7662）是**普通结构体（非 ORM）**，字段不变，按聚合归位到 `oauth2` Domain 包，关键字段由值对象包装。**当前定义在 `OAuth2Plugin/include/oauth2/storage/IOAuth2Storage.h`（不是 `OAuth2Types.h`；后者仅含枚举 `ClientType`/`GrantType`/`OAuth2Error`）**——M2b（Task 17）迁移时以此为源（评审 A5）。

### ORM 模型（归 `storage-postgres`，评审 F2）

ORM 模型（`Oauth2*` / `Users` / `Roles` 等）是 `drogon::orm` 生成类型，**必须归 `libs/storage-postgres` 适配层，不得进 Domain**（否则违反「Domain 禁 Drogon」这条铁律）。仍不可手改、经 `drogon_ctl` 生成。Postgres 仓储实现负责 ORM 模型 ↔ Domain DTO/值对象的双向映射。这也解释了为何 Domain 层可以做到零 Drogon 依赖：所有 `drogon::orm` 触点都被隔离在 storage-postgres。（`OAuth2Plugin/models_backup/` 是 ORM 重生成时的**临时备份目录**，应加入 `.gitignore` 不入库、视为 ephemeral，**非迁移源、不迁移**，评审点 1。）

---

## 9. 依赖管理与跨平台（#5 #6 已核实）

### 9.1 统一 Conan 2.x（三平台唯一依赖入口）

| 项 | 现状 | 目标 |
|----|------|------|
| 依赖描述 | `conanfile.txt` | `conanfile.py`（支持 options：`with_identity` / `with_social` 等条件依赖） |
| Drogon | Linux 源码编译 / Windows Conan / macOS brew（三套不一致） | **三平台统一 Conan Center `drogon/1.9.13`**（取消源码编译） |
| OpenSSL | 混乱：`conanfile.txt` 固定 `1.1.1t`（Windows 路径），但 Linux CI 用 apt `libssl-dev` 实为 **3.0.2**——**项目实际已跑在 3.x 上**（评审 F8 更正） | 统一 **3.5.x LTS**（跳过将于 2026-09 EOL 的 3.0；libcurl `with_ssl=openssl` 同步） |
| jsoncpp | 1.9.4 | 对齐 Drogon recipe 的 1.9.5 |
| 构建预设 | build/ 内生成、未入库 | 顶层 `CMakePresets.json` 入库：`linux-release`/`windows-msvc`/`macos-arm64`/`*-asan`/`*-tsan` |
| 缓存 | CI 全禁用 | 恢复缓存，用 `conan.lock` + 内容哈希键保证正确性 |

> 核实依据：Conan Center drogon recipe 声明 `openssl/[>=1.1 <4]`（支持 3.x）；Homebrew drogon 依赖 `openssl@3`；Conan Center 已有 `drogon/1.9.13`（= 项目 pin 版本）。**最强直接证据（F8）：本项目 Linux CI 现已用 apt 的 OpenSSL 3.0.2 构建并通过全部测试**——即 Drogon 1.9.13 + 本项目在 3.x 上已被验证可用，升级 3.5 主要是版本对齐而非兼容性攻关。
> 待落地验证：Conan drogon 包的 `drogon_ctl`（`with_ctl`）能否用于 ORM 生成；若不可用则单独安装 `drogon_ctl`（生成是离线开发动作，不影响运行期依赖）。

### 9.2 跨平台矩阵

| 平台 | 编译器 | 架构 | 测试存储后端 |
|------|--------|------|------------|
| Linux | GCC 11 / Clang 15 | x64, arm64 | Postgres + Redis 全量 |
| Windows | MSVC 2022 | x64 | Memory |
| macOS | AppleClang | arm64 | Memory（可选 Postgres） |

保留 `cmake/Compatibility.cmake` 的 macOS 纯 C++17 处理（`codecvt` 兼容）。

### 9.3 OpenSSL 3.x 迁移评估（基于代码扫描）

说明：本节仅评估 **OpenSSL API 层面**的弃用迁移；「Domain 去 `drogon::utils`」是另一块**更大**的工作，见 §5.6，勿混为一谈。就 OpenSSL API 本身而言，全项目仅 **1 处**需迁移：

| 位置 | 弃用 API | 迁移方案 | 工作量 |
|------|---------|---------|--------|
| `JwkManager::getPublicKeyComponents()` | `EVP_PKEY_get1_RSA` / `RSA_get0_key` / `BN_num_bytes` / `BN_bn2bin` | `EVP_PKEY_get_bn_param(pkey, OSSL_PKEY_PARAM_RSA_N/E, &bn)` | ~15 行 |

其余全部无影响：`PKCS5_PBKDF2_HMAC`、一次性 `HMAC()`、`EVP_DigestSign*`、`EVP_PKEY_keygen*`、`PEM_read_bio_*`、`RAND_bytes` 在 3.x 均未弃用。RSA/SHA/PBKDF2 均在默认 provider，无需 legacy provider。JWKS 输出字节保持不变（`BN_bn2bin` 语义等价）。`signJwt` 并发安全在 3.x 依然成立。

---

## Error Handling

- 框架无关的错误目录 `ErrorCatalog` + 错误信封 `ErrorEnvelope` 下沉到 `common::error`（现为 `common::error`，去除对 Drogon 响应类型的直接依赖，改由 Adapter 层渲染 HTTP）。
- 稳定字符串错误码（如 `AUTH_INVALID_CREDENTIALS`）作为唯一真实来源，禁用整数码散落。
- OAuth2 协议错误保持 RFC 6749 §5.2 语义（`server_error` 等）；应用端点统一 Error Envelope。
- 保留现有 Catalog 启动时不变量校验（`validateInvariants()`，缺陷构建 fail-fast）。
- 保留 dev/staging/prod 的敏感信息隔离（生产模式不泄露诊断细节）。
- **前端错误契约共享源（评审 L2）**：`ErrorCatalog` 下沉 `common::error` 后作为错误码唯一真实来源，须提供供前端消费的机制（构建期从 Catalog 生成 TS/JSON 供 `frontends/*`），目录移动（M8）后更新共享路径；沿用现有前端 fast-check errorAdapter 跨应用一致性属性测试。

## Correctness Properties

沿用并迁移现有基于属性的测试（Property 1–14）到对应库层，作为发布门禁。核心属性如下：

### Property 1: 错误目录不变量

每个目录条目满足结构不变量，码与数字码全局唯一，OAuth 允许集恰好被覆盖一次。

### Property 2: HTTP 状态一致性

运行期 HTTP 状态等于目录登记值，且与类别映射一致。

### Property 3: Error Envelope 完整性

信封序列化往返无损、结构不变量成立、numeric_code 正确省略/呈现。

### Property 4: 生产模式安全隔离

生产模式下不泄露诊断细节；该属性构造性地强制生产模式，与运行配置无关。

### Property 5: 协议与请求标识合规

OAuth2 错误满足 RFC 6749 §5.2；Request-Id 正确解析或生成。

### Property 6: 仓储/端口契约一致（新增）

Memory / Redis / Postgres 三实现对同一契约测试套件行为一致（M1 契约测试）。

## Testing Strategy

（原「测试架构」）

**核心变革：测试消费库产物，而非 `GLOB_RECURSE` 重编所有源码**（现状掩盖了链接边界问题）。

| 层级 | 位置 | 依赖 | 链接方式 | CTest label |
|------|------|------|---------|-------------|
| 单元（Domain 纯逻辑） | `libs/*/test/` | 无 DB/无 drogon | 链接对应库 | `Unit` |
| 契约（仓储/端口） | `tests/contract/` | Memory/Redis/PG | 链接各库 | `Contract` |
| 集成 | `tests/integration/` | DB+Redis | 链接库 | `Integration` |
| 安全 | `tests/security/` | 视情况 | 链接库 | `Security` |
| 性能 | `tests/performance/` | Memory | 链接库 | `Performance` |
| 后端 E2E | `tests/e2e-backend/` | 全栈 | 链接产品库 | `E2E` |
| 前端 E2E | `tests/e2e-frontend/` | Playwright | — | — |
| **SDK 冒烟** | `examples/third-party-host/` | 仅 `find_package` | 仅头文件 + 库 | `SdkSmoke` |

- 保留现有错误标准化属性测试（Property 1~14）、`TestTransaction` RAII 回滚、ASan/TSan 单目标插桩（改为按库粒度可选）。
- 保留前端 fast-check 属性测试（errorAdapter 跨应用一致性）。
- **新增 SDK 冒烟**：`examples/third-party-host` 作为 CI job，`find_package(authforge-oauth2)` 后跑一个授权码流——「库能被外部消费」的唯一可信证明。

---

## 11. CI/CD 设计

重构为「可复用 workflow + 分阶段门禁」：

```
.github/workflows/
├── ci.yml            # PR 主流水线（矩阵调用可复用 workflow）
├── _build-test.yml   # 可复用：单平台 build + 分层 ctest
├── _frontend.yml     # 可复用：前端 lint + vitest 属性测试
├── _sdk-smoke.yml    # 可复用：SDK find_package 集成冒烟
├── release.yml       # tag 触发：多架构镜像 + SDK 库产物 + Helm + SBOM + 签名
├── nightly.yml       # 定时：ASan/TSan + 性能基线 + 依赖审计
└── security.yml      # CodeQL + 依赖漏洞/EOL 扫描
```

分阶段门禁：

1. **快速门**（无需 DB）：clang-format、clang-tidy、arch-guard、前端属性测试、Domain 单测、命名校验、manage 命令对齐。
2. **主门**：三平台矩阵 build + 契约/集成/E2E。
3. **发布门**：错误标准化属性测试全绿（沿用 Req 12.7）、SDK 冒烟通过、OpenAPI 校验、性能基线不回退。

发布产物：多架构镜像（amd64+arm64，buildx）、SDK（库 + 头 + `authforge-*Config.cmake`）、Helm chart + 生产 Compose、语义化版本 + 自动 CHANGELOG、SBOM + 镜像签名。

改进：恢复被禁用的 Drogon/Conan 缓存（lockfile 保证正确性），Linux 改用 Conan drogon 包，显著缩短 CI。

---

## 12. 自动化工具（tools/）

在现有 `naming_validator.sh` / `manage-parity-check.sh` 基础上扩充：

| 工具 | 作用 | 触发 |
|------|------|------|
| `manage.ps1/.sh` | 统一入口，命令随新结构更新 | 手动 |
| ORM 生成器封装 | `drogon_ctl` 生成 → 搬到 `models/` + 校验未手改 | 手动/pre-commit |
| **arch-guard** | 静态检查依赖方向：Domain 层禁 `#include <drogon/`（允许 jsoncpp）；oauth2↔identity 无互相 include | CI 快速门 |
| **api-diff** | SDK 导出头 API 快照 diff，破坏性变更需版本升级 | CI |
| OpenAPI 同步校验 | 代码路由 vs `openapi.yaml` 一致性 | CI |
| 错误目录一致性 | Catalog ↔ doc 校验（沿用） | CI |
| 迁移校验器 | 迁移文件顺序/幂等/回滚脚本存在性 | CI |
| 依赖漏洞/EOL 扫描 | 拦截 OpenSSL 1.1.1 类 EOL 依赖 | nightly + PR |

**arch-guard 是新架构的护栏**：机器强制「Domain 不依赖 Drogon」，否则解耦随时间腐化。

---

## SDK 运行时契约（线程 / ABI / 异常 / 日志，评审 F9）

为「生产级可复用 SDK」补齐以下对外契约（原设计缺失）：

- **线程模型**：Domain 服务不自持事件循环；所有异步经回调返回，回调可能在任意 Drogon IO 线程触发。SDK 头文档须声明「回调线程非调用线程」，消费者不得假设线程亲和性。`JwkManager` 等只读单例遵循「init-once-then-read-only」，构造在服务请求前完成。
- **ABI 稳定性**：v1.x 期间仅 `find_package` 源码集成，**不承诺二进制 ABI**，只承诺**源码级 API** 的语义化版本；跨编译器/STL 混用二进制不在支持范围（进入 Conan 二进制包阶段再定 ABI 策略）。
- **异常安全约定**：Domain 公共 API 以 `Result<T,Error>` 返回可预期错误，不用异常表达业务失败；仅在不可恢复的编程错误（契约违反）抛异常。存储底层异常（`DrogonDbException`）必须在 Adapter 层捕获转 `Error`，**不得穿透到 Domain 回调**。
- **日志抽象**：Domain 经 `ILogger` 端口输出，不直接用 Drogon `LOG_*` 宏（见 §5.6）；默认提供 Drogon 日志适配实现，消费者可替换。
- **依赖声明**：WebAuthn（FIDO2）所需的加密/CBOR 依赖须在 `identity` 包的 conanfile options 显式声明（`with_webauthn`），当前隐式依赖需梳理，避免消费者踩缺库。

---

## 13. 版本与发布契约

- 统一语义化版本，**重置为 v1.0.0**（新 SDK 正式起点）。单仓库统一版本：三包共享仓库版本，未来需要再拆独立版本线。
- SDK API 稳定策略：公共头 `include/authforge/**` 遵循 SemVer，破坏性变更 → major。由 api-diff 工具强制。
- 弃用策略：`[[deprecated]]` + 至少一个 minor 周期过渡。
- Conan：**先提供 `find_package` 源码集成**，每包导出独立 `authforge-*Config.cmake`；后续发布到 Conan Center / 私有 registry。

---

## 14. 关键风险与缓解

| 风险 | 缓解 |
|------|------|
| 大规模解耦引入回归 | 每里程碑 CI 全绿；现有测试作安全网；小步提交 |
| Domain 去 Drogon 化（M2）风险最高 | 先抽接口再搬实现；端口化基础设施；先补 Domain 纯单测再迁移 |
| ORM 模型不可改 | 解耦靠接口分层，不动模型；生成器封装校验 |
| oauth2↔identity 意外耦合 | 端口下沉 common + arch-guard 强制 |
| Conan drogon_ctl 可用性未知 | M0 实测，不可用则单独装 drogon_ctl |
| 目录大规模移动破坏引用 | 放最后（M8），用 IDE 重构 + smart move 保证**代码**引用更新 |
| **非代码路径引用随目录移动失效（含 H2 时序修正）** | M0 先建 `paths.env` 集中路径定义；在 M2a/M3（构建产物路径变化）各挂「同步路径引用」任务、M8 的 Task 42-44 做顶层改名对齐；**区分 CI 覆盖路径（CI 全绿天然拦截）与本地/agent 路径（不进 CI，须主动改）**，后者不能靠 CI 兜底 |
| **静态库丢弃 Drogon 注册符号致路由 404（F1，高）** | whole-archive 链接（§5.5）；SDK 冒烟实发 HTTP 断言路由注册；M4 测试同样 whole-archive |
| **ORM 模型误入 Domain 破坏去 Drogon（F2，高）** | ORM 归 storage-postgres，Domain 只用 DTO/值对象；arch-guard 强制 |
| **去 drogon::utils 工作量被低估（F3，高）** | 端口清单 §5.6；M2 拆出 M2a 专列端口化；`CryptoUtils.h` 内联须逐调用点改注入 |
| **契约测试三实现一致不现实（F5）** | 分「功能契约」+「原子性/事务契约」两档，按能力标志运行（§7.3） |
| **测试库化的 views/私有头/白盒依赖（F6）** | M4 处理 `drogon_create_views` 生成、导出必要测试支持头、剥离白盒依赖 |
| **secrets 管理与 legacy 密码迁移兼容（F10）** | 敏感项仅经 env/secret store、禁落盘日志；保留 `PasswordHasher` legacy SHA-256 校验与 `needsRehash` 平滑升级路径，迁移测试覆盖 |
| **config 插件按名反射，改名/迁库致启动期失效（H1，高）** | 保留 `OAuth2Plugin` 类名 + whole-archive 保活插件符号（方案 A）；或 main.cc 显式装配并迁 config 块（方案 B）；§5.7；Task 21/22 验收含 config 驱动实例化 |
| **CachedOAuth2Storage 装饰器拆分后无归属（H3，中）** | §7.4 定 per-repository 装饰；**UAF 已在 HEAD 修复（`enable_shared_from_this`，提交 `30a1d1e`）——非待修缺陷**；迁移须**保留**该安全模式，`CategoryC_CachedStorageUafTest` 作回归门控 |
| **全局单例 `getPlugin<OAuth2Plugin>()` → 去单例化改动量大（H4，中，**已完成，方案与原设想不同**）** | 原设想是"改为构造注入"，但插件由 config 反射构造、必须晚于 controller 注册完成，二者存在框架级先后顺序限制，构造函数注入不可行。**已落地方案**：两阶段装配——controller/filter 仍按 §5.5 的 `AutoCreation=false` 机制原样构造注册；新增 `setPlugin(OAuth2Plugin*)` setter，在 `registerBeginningAdvice` 回调（插件已构造完成的时间点）里用 `drogon::DrClassMap::getSingleInstance<T>()` 取到已注册的同一单例，逐个调用 `setPlugin()`；每个 handler 内部经 `resolvePlugin()`（`plugin_ ? plugin_ : getPlugin<OAuth2Plugin>()`）取用，缓存命中时不再每次查询全局单例，未设置时向后兼容回退到原查找方式。已验证：6 个 controller + 2 个 filter（15 处生产调用点）改造完成，`ctest` 226/288 区间全绿（视里程碑推进），真实服务器验证行为一致 |
| **长链重构无中途可发布策略（H6，中）** | §14.1 分里程碑可发布性；M2a 逐端口逐调用点可独立合并；M8 脚本化一次性迁移 + 迁移前打 tag，整体成功或整体回退 |

### 14.1 分里程碑可发布性（评审 H6）

避免 all-or-nothing。逐里程碑标注「停在此里程碑产品是否仍可交付」：

| 里程碑 | 停在此处产品可发布？ | 说明 |
|--------|:---:|------|
| M0 | 是 | 仅构建/依赖变更，功能不变 |
| M1 | 是 | 接口拆分 + 现有测试全绿，行为等价 |
| M2a | **过程中否/完成后是** | 去 drogon::utils 是横切改动，须**逐端口逐调用点**推进使中间提交可编译；里程碑完成才可发布 |
| M2b/M2.5 | 是 | 每步 CI 全绿，功能等价 |
| M3 | 是 | 装配器 + whole-archive，路由/功能等价 |
| M4/M5/M6/M7 | 是 | 增量增强，可随时停 |
| M8 | **原子** | 目录/命名空间/版本一次性切换：脚本化迁移 + 迁移前打 tag，只允许整体成功或整体回退 |

---

## 15. 待落地时确认的细节

1. 端口接口下沉 `common` 的最终形态（方案 A 已选定，细化每个端口签名在实施阶段）。
2. ~~Conan `drogon_ctl` 实测结果 → 决定 ORM 生成流程。~~ **已实测，见 §15.1（Task 2 结论）。**
3. OpenSSL 3.5 LTS 具体补丁版本（落地时取当时受支持的 LTS）。
4. 插件注册迁移方案 A/B 抉择（§5.7）——决定 config 4 份是否零改动。
5. `OpenApiGenerator`（385 行）**已核实不使用 Drogon 路由自省**（手动 `addEndpoint(EndpointInfo)` + 输出 `Json::Value`），按设计自身规则归 **`apps/server`**（决策已定，评审 L1/B1 关闭；当前误置于 `OAuth2Plugin` 伪领域层，M8 迁出）。
6. `observability-prometheus`（评审 H7/B3 更正）：**当前无自研导出器，4 份 config 均用原生 `drogon::plugin::PromExporter`，不存在「双轨」冲突**。该包为**可选新增**，仅当需脱 Drogon 的 metrics 时才做，否则保留原生插件——降级为延后项。

### 15.1 Conan drogon 包 `drogon_ctl` 可用性实测结论（Task 2，本任务新增）

**结论：`drogon_ctl`（Conan `drogon/1.9.13`，`with_ctl=True`）在 Windows 上可用，且完整跑通了项目现有 ORM 生成流程，产物与当前入库模型逐字节一致。M0 风险「Conan drogon_ctl 可用性未知」在 Windows 平台上解除，无需单独安装脚本兜底。Linux/macOS 未验证，留待对应平台 CI/开发环境验证（见下方「未验证部分」）。**

**验证过程（Windows，本机实测）**：

1. 用 Task 1 的 `conanfile.py`（`drogon/*:with_ctl=True`）执行 `conan install`，drogon 从源码构建（Conan Center 无预编译二进制，`build_type=Release`/MSVC 194/`compiler.cppstd=17`），产出 `drogon_ctl.exe` 于 Conan 包目录的 `bin/` 下。
2. 直接调用该二进制的 `drogon_ctl version`，确认版本 1.9.13、`postgresql: yes`、`ssl/tls backend: OpenSSL`，功能齐全。
3. 确认本机 PostgreSQL 服务已在运行（`pg_isready` 通过）且 `oauth2_db` 数据库中 21 张表存在，含项目现有 ORM 生成流程依赖的全部 14 张表。
4. 复用项目现有 ORM 配置 `OAuth2Plugin/src/models/model.json`（即 `.claude/skills/orm-gen/SKILL.md`、`.agent/workflows/orm-gen.md`、`scripts/backend/generate_models.bat` 三处一致引用的真实配置源，含 `user=oauth2_user` 凭据与 14 表 + 关系定义），在隔离的临时目录中执行 `drogon_ctl create model . -o <output>`，等价于现有脚本调用的命令形态。
5. 生成成功：14 张表 × (`.h` + `.cc`) = 28 个模型文件全部生成，连接数据库、外键自动探测（如 `user_roles.user_id -> users.id`）均正常工作。
6. 逐文件比对生成产物与仓库中已入库的模型文件（`OAuth2Plugin/include/oauth2/models/*.h` + `OAuth2Plugin/src/models/*.cc`）：抽样比对的文件（`Oauth2Clients.h/.cc`、`Users.h`、`Oauth2UserConsents.h`）**逐字节一致（`Compare-Object` 无差异输出）**，证明 Conan 构建出的 `drogon_ctl` 与当前项目模型的生成来源一致、行为无漂移。
7. 全部临时文件（探测目录、生成产物、conan 探测生成的 CMake 辅助文件）已清理，未污染工作区；`git status` 确认除 Task 1 遗留的未跟踪 `conan.lock`/`conanfile.py` 外无新增文件。

**验证到什么程度**：
- 二进制定位与调用：**通过**（从 Conan 包缓存路径直接调用 `.exe`，未依赖系统 PATH；后续落地时项目脚本需决定是把该 `bin/` 目录纳入 PATH，还是让 `generate_models.bat`/`generate_models.sh` 显式探测 Conan 包路径调用——本任务只验证二进制本身可用，未改造脚本，脚本改造留给 Task 18/M2b 或后续路径同步任务）。
- 数据库连接与真实表结构：**通过**，非模拟。本机有实际运行的 PostgreSQL 实例和已初始化的 `oauth2_db`，全程用真实凭据连接、真实建表结构生成模型，未伪造结果。
- 生成结果与现有模型一致性：**通过**，抽样文件逐字节相同。

**决策**：ORM 生成流程不需要单独安装 `drogon_ctl` 的兜底方案（Windows）；`with_ctl=True` 保留在 `conanfile.py` 即可满足 M0 Task 2 的验收要求。Task 9/18（ORM 模型迁 `storage-postgres`）可按原计划推进，不必改变生成器来源。

**未能验证的部分（如实说明，不臆造）**：
- **Linux / macOS 未验证**：本机为 Windows 环境，无法在这两个平台上实际执行 `conan install --build=missing` 构建 drogon 并验证 `drogon_ctl`。根据 Conan Center 的 `drogon/1.9.13` recipe，`with_ctl` 选项在配方层面对三平台是同一套源码构建逻辑（无平台特定禁用条件），且项目现有 Linux CI 已能从 apt 源码路径编译出可用的 Drogon（`ci-linux.yml`），侧面支持 Linux 可行性，但这是**推断而非实测**，需要在真正的三平台 CI 跑一次 M0 Task 6（改造三平台 CI 使用统一 Conan）时才能坐实。macOS 同理未验证。
- **未测试通过网络远程 CI 环境的行为**（如 GitHub Actions runner 的权限、缓存行为），本次只验证本机开发环境。
- 由于 Windows 验证已完整跑通且逐字节一致，暂不提供三平台单独安装 `drogon_ctl` 的兜底脚本；如 Task 6（三平台 CI 改造）实测发现 Linux/macOS 上 `with_ctl` 构建失败，需回来补上兜底安装脚本（届时更新本节）。
