# AuthForge 多语言客户端 SDK 生成方案设计

> **版本**: v1.2（M1/M2 立项修订：补充 §十一 生成器实测、PyPI 命名、布局与 CI 落点）
> **日期**: 2026-08-05（v1.0）/ 2026-08-16（v1.1 M0 修订）/ 2026-08-18（v1.2 M1/M2 修订）
> **文档性质**: 技术设计（Phase 1 落地蓝图，**非代码**——实施见 §七 milestone，各 milestone 单独立项）
> **上游规划**: [演进方案 §三 Phase 1](../productization-evolution-plan.md)「多语言 HTTP 客户端 SDK」
> **姊妹设计**: [基准设施设计](../in-progress/benchmark-facility-design.md)（Phase 0，与本设施协同——见附录 D）
> **承重背景**: 本设施的**前提**是 OpenAPI spec 治理——当前 spec 处于"两源漂移 + 一个死文件"状态（见 §二），不先治 spec 就生成客户端等于在流沙上盖楼。

---

## 零、TL;DR

**两层架构，Layer 1（spec 治理）必须先于 Layer 2（客户端生成）：**

- **Layer 1 — Spec 单一源治理**：删 3 个月前的死孤儿 `docs/backend/api/openapi.json`；定 `apps/server/openapi.yaml` 为唯一权威（已是 CI lint + 端点签名门的输入）；**⚠️ 补齐 YAML 的 requestBody/response/schema 内容**（当前是"路径全、内容稀疏"的壳，无法驱动客户端生成——D1.5）；新增**YAML↔代码一致性门**（关闭当前最大治理漏洞——C++ 改端点而忘改 YAML 无人发现）；引入 **oasdiff 破坏性变更门**（客户端可发布的前提）。
- **Layer 2 — 客户端生成**：用 **openapi-python-client**（Pydantic v2 + httpx）+ **oapi-codegen**（Go 社区标准），两者均无 JVM；**手写 auth 层**（Go 用 `golang.org/x/oauth2/clientcredentials`，Python 用 httpx transport wrapper，**不重造 token 逻辑**）；生成代码提交进 `clients/python` + `clients/go`，release 时生成并发布 PyPI / Go module proxy。

> **关键判断**：本设计**不**用 openapi-generator 全生成 token 生命周期——OAuth2 服务器的 token 生命周期（refresh 旋转、PKCE、lockout）太敏感太变量，盲目生成不安全（ZITADEL/Logto 实践验证此判断；Ory 全生成模型对 AuthForge 不适用，理由见 D5）。

---

## 一、目标与非目标

### 1.1 目标

| # | 目标 | 衡量 |
|---|------|------|
| G1 | **确立 OpenAPI spec 单一源 + 治理闭环**：消除两源漂移、补 spec↔代码一致性门、补 HTTP API 面的破坏性变更守护 | §六 AC1/AC2/AC3 |
| G2 | **产出可发布的 Python + Go 客户端**：第三方 `pip install authforge` / `go get` 即可用 | §六 AC7 |
| G3 | **补 §3.2「API 稳定性」卖点的工程证据**：调研报告把"API 稳定性"列为卖点但工程上 api-diff 只管 C++ 头、HTTP 面无守护——本设计填补这个缺口 | oasdiff 门 + SemVer 联动 |
| G4 | **与基准设施协同**：客户端 auth 层可作为第三方复现性能基准的工具（附录 D） | M4 端到端示例 |

### 1.2 非目标

| # | 非目标 | 为什么 / 归属 |
|---|--------|--------------|
| N1 | 改 C++ `OpenApiGenerator` 的生成逻辑（含其 `security` object bug + 缺 `clientCredentialsAuth`） | 那是另一个重构；本设计让 generated JSON 降级为派生产物，绕开它（D1）。bug 已提 [#41](https://github.com/lucaswang420/authforge/issues/41) 跟踪。**例外**：D1.5 的 YAML 补齐可能选择修生成器（而非手维护 YAML）作为补齐机制——此子决策由 M0 立项时定，若选修生成器则 #41 一并修，且 D1 的"JSON 降级"需重评 |
| N2 | 做 TypeScript 客户端 | 前端生态（hey-api/orval），与后端性能叙事弱相关；Phase 1 聚焦 Python+Go |
| N3 | 自动生成 token 生命周期逻辑 | 手写（D5/D7）；全生成对 OAuth2 服务器不安全 |
| N4 | 做 Ory 式全生成多语言 SDK | Ory 模型因 auth 在服务端才适用；AuthForge 是 OAuth2 服务器本身，token 生命周期在客户端，不能盲目生成 |
| N5 | 改 Swagger UI | 保留现状（YAML 单源后 UI 仍可用，或可改读 YAML——非本设计范围） |

---

## 二、背景：OpenAPI 治理现状（承重前提）

> 调研颠覆了"三份 spec 碎片化"的初判——实际是**两源漂移 + 一个死文件**。所有事实 2026-08-05 核实，带 `file:line`。

### 2.1 三 OpenAPI 文件的真实拓扑

| 文件 | 路径数 | **内容完整度**（requestBody / response schema / schema 定义） | 来源 | CI 校验 | 消费方 | stale 程度 |
|------|--------|---|------|---------|--------|-----------|
| **generated JSON** | 58 路径 / 71 操作 | **极稀疏**：0 requestBody / 4 response schema / 仅 2 schema（`Error`、`TokenResponse`） | C++ `OpenApiGenerator`（`libs/drogon/src/observability/openapi/OpenApiGenerator.cc`）启动时生成 | **否**（仅指纹测试间接捕获，见 §2.3） | Swagger UI 自文档 | 当前（今日提交） |
| **手维护 YAML** | 56 路径 / 69 操作 | **稀疏**：6 requestBody / 9 response schema / 仅 2 schema / 仅 `bearerAuth`（缺 `clientCredentialsAuth`） | 人工编辑（`.claude/skills/openapi-update` AI 提示词） | **是**（`ci.yml:68-71` 的 `openapi-spec-validator`） | `tools/diff-endpoint-baseline.py:45` 端点签名门 + 将来的客户端生成 | 当前（今日提交） |
| **死孤儿 JSON** | 8 路径 | （同 generated JSON 旧形态） | generated JSON 的旧快照 | **否** | **零引用**（grep 全仓零命中） | **3 个月前**（2026-05-14，`de288ef`） |

三文件均声明 `openapi: 3.0.0` / `info.version: 1.0.0`，但 `info.version` **不随 staleness 变化**（死孤儿仍写 1.0.0），故它不可作 staleness 信号。

> ⚠️ **承重警告**：上表的"内容完整度"列是 D1.5 的依据。**路径数对 ≠ spec 可用**——YAML 路径覆盖足（69 操作），但 requestBody/schema 严重缺失，**无法直接驱动客户端生成**（生成的客户端拿不到 `/oauth2/token` 该发哪些表单字段、拿不到强类型响应）。这是 M0 的实质工作量，详见 D1.5。

### 2.2 两活源的真实漂移

generated JSON（71）与 YAML（69）的差异：
- **YAML 少 2 个 `/docs/api/*` 自文档路径**（`GET /docs/api/`、`GET /docs/api/openapi.json`，由 `ApiDocController` 注册，YAML 从未补）。
- **`security` 字段形态分歧**：generated JSON 输出成 **object**（`OpenApiGenerator.cc:195-200` 的 `Json::Value` 构造，**OpenAPI 3 要求 array，属畸形**——已提 [#41](https://github.com/lucaswang420/authforge/issues/41)）；YAML 输出正确的 **array-of-objects**。`openapi-spec-validator` 只跑 YAML，故 JSON 的畸形未被发现。
- **`clientCredentialsAuth` scheme 缺失**：generated JSON 只硬编码 `bearerAuth`（`OpenApiGenerator.cc:122-128`），缺 introspect/revoke/token 实际需要的 client 凭证 scheme——[#41](https://github.com/lucaswang420/authforge/issues/41) 一并覆盖。YAML 也只有 `bearerAuth`（同样缺）。
- 死孤儿 JSON 与 generated JSON 的差异：死孤儿缺 50 个 2026-05 后新增的路径（WebAuthn/MFA/admin CRUD/OIDC discovery 等）+ 缺 `clientCredentialsAuth`。

### 2.3 C++ 代码的权威 API 面（治本设计的钥匙）

指纹测试 `tests/integration/concurrency/Property4_OpenApiValidationBaselineTest.cc:90-168`（`Integration_P1_OpenApiSpec_Property4_3_1_PathMethodFingerprint_Baseline`）：
- 链接全部 controller，调用 `OpenApiGenerator::generateOpenApiSpec()`，断言排序后的 `METHOD path` 集**逐字节等于**硬编码的 71 行字符串。
- 这 71 操作（含 `GET /docs/api/openapi.json`）= **C++ 代码真实注册的 API 面，权威且经测试**。
- **但它没和 YAML 比对**——这是漂移的制度性根因：C++ 加/删端点，指纹测试会捕获（强迫开发者更新测试 baseline），但 YAML 不会自动跟，全靠 `.claude/skills/openapi-update` AI 提示词"记得改"。这是当前最大治理漏洞。

### 2.4 治理现状总结（7 条核实发现）

1. **死孤儿 `docs/backend/api/openapi.json` 零引用**：grep 全仓（.cc/.h/.cmake/.sh/.py/.md）零命中；其兄弟 `swagger-ui/` 目录被 `apps/server/CMakeLists.txt:76-77` 复制到 build，但 JSON 本身不复制、不被服务、不被引用。
2. **generated JSON 在 CI 无校验**：仅 YAML 经 `openapi-spec-validator` 结构校验（且只校验 OpenAPI schema 合法性，不校验与代码一致）。
3. **api-diff 不碰 OpenAPI**：`tools/api-diff/api_diff.py:2-7,59-65` 只扫 `libs/*/include/authforge/**` 的 7 个 C++ 库头（`api-baseline.txt` 275KB 是头骨架）。HTTP API 面**无 SemVer 守护**。
4. **`validate-openapi.sh` 是死代码**：零 CI 引用（`.github/workflows/` 全无），只查 generated JSON 的 4 个顶层 key（`openapi/info/paths/servers`），无 schema 校验、无 lint、无 diff、无 Windows 路径。
5. **spec 未发布、无生成、零消费**：`release.yml` 不打包任何 openapi 文件；前端 `frontends/admin|user` 手写 fetch（`grep -rniE 'openapi|swagger' frontends/` 仅命中一个 Swagger UI 的 Playwright 测试）；spec 纯文档用途。
6. **OAuth2 scheme = `http: bearer/basic`**（非 `oauth2` flows）：生成器不会自动生成 token 生命周期——这恰是推荐状态（D5）。
7. **⚠️ 两个活源都是"路径全、内容稀疏"的壳**（D1.5 依据）：YAML 69 操作仅 6 有 requestBody / 9 有 response schema / 2 个 schema 定义；generated JSON 71 操作 **0** requestBody。**spec 当前不能直接驱动客户端生成**——这是 M0 的实质工作量，不只是删文件 + 加门。

> **治理现状一句话**：唯一在 CI 生效的保护 = "`apps/server/openapi.yaml` 通过 `openapi-spec-validator` 的结构校验"。仅此而已。两源漂移、spec↔代码脱节、破坏性变更无守护、死文件未清、**且两个活源内容都稀疏到无法驱动客户端生成**——这是本设计要填的治理坑。

---

## 三、分支决策

四个基础决策，每项选推荐项，带否决备选的理由。

### 3.1 决策 1 — 单一源 = YAML（generated JSON 降级）

| 选项 | 取舍 |
|------|------|
| ✅ **YAML 为唯一权威** | 它是 CI lint 的、是端点签名门（`diff-endpoint-baseline.py`）的输入、security 形态正确、已有 2 个消费方。generated JSON 降级为 Swagger UI 用的派生产物。**⚠️ 但 YAML 当前内容稀疏（69 操作仅 6 requestBody/9 response schema），需先补齐（D1.5）才能驱动客户端生成** |
| ❌ JSON 为唯一源 | generated JSON 最贴近代码，但：CI 不校验、有 security 畸形 bug（[#41](https://github.com/lucaswang420/authforge/issues/41)）、内容比 YAML 更稀疏（71 操作 **0** requestBody）、需重起生产级生成流程、`OpenApiGenerator.cc` 不在 CI 跑。改造代价远高于定 YAML 单源 + 补齐 |
| ❌ 双源 + 一致性门 | 把现在的漂移制度化，两个源永远要手动同步，复杂度最高 |

### 3.2 决策 2 — 引入 oasdiff 破坏性变更门 + SemVer 联动

| 选项 | 取舍 |
|------|------|
| ✅ **引入** | 客户端 SDK 可发布的前提——无它则 SDK 版本承诺无意义。也补 §3.2「API 稳定性」卖点的工程证据（api-diff 只管 C++ 头，HTTP 面当前零守护） |
| ❌ 本次只治理不门控 | 更快落地，但 SDK 发布承诺会有缺口，且 §3.2 卖点仍无证据 |

### 3.3 决策 3 — 客户端范围 = Python + Go

| 选项 | 取舍 |
|------|------|
| ✅ **Python + Go** | 最主流非 C++ 栈，缓解"C++ 人才稀缺"风险（调研报告 §七最高风险）。每语言配手写 auth 层 |
| ❌ 仅 Python | 最聚焦最快，但覆盖面窄，Go 是云原生/IoT 目标客户的核心栈 |
| ❌ Python + Go + TypeScript | 覆盖最广，但 TS 主要服务前端生态（hey-api/orval），与后端性能叙事弱相关，体量最大 |

### 3.4 决策 4 — 生成代码提交进 git + 漂移检测门

| 选项 | 取舍 |
|------|------|
| ✅ **提交 + 漂移门** | release tag 自包含（消费者 `pip install`/`go get` 不需跑生成器）、生成器非消费者构建依赖。Ory/主流多语言 SDK 做法。CI 加漂移门防 stale |
| ❌ 不提交，CI 即时生成 | 无评审噪声、spec 真单源，但 release tag 不自包含、生成器进构建路径、确定性要求高 |

---

## 四、Layer 1：Spec 单一源治理（**地基，必须先于客户端**）

五个子决策 D1–D1.5、D2–D4，每个带代码依据。

### D1 — YAML 为唯一权威，generated JSON 降级

**依据**：§2.1 表——YAML 已是 CI lint + 端点签名门的输入，security 形态正确；generated JSON 在 CI 无校验且有畸形 bug（见 §九 + [#41](https://github.com/lucaswang420/authforge/issues/41)）。

**决策**：
- `apps/server/openapi.yaml` = **客户端生成 + 端点门 + 破坏性变更门**的唯一输入。
- generated `apps/server/docs/api/openapi.json` = Swagger UI 自文档用的运行时派生产物，**不再当契约源**。
- **处理 YAML 少的 2 个 `/docs/api/*` 路径**：这两端点是 API 服务器的自文档（Swagger UI / spec 自身），对 SDK 消费者无意义。**显式从客户端 SDK 面排除**，并在一致性门（D3）里硬编码此例外清单，文档化排除规则。

> ⚠️ **但 YAML 目前远未"客户端生成就绪"——见 D1.5。** D1 定的是"谁是权威"，D1.5 解决的是"这个权威当前够不够用"。两者都属 Layer 1，且 D1.5 是 M0 的实质工作量。

### D1.5 — YAML 内容补齐（**当前是稀疏壳，必须先补全才能驱动客户端生成**）

**依据（2026-08-05 核实的稀疏度）**：YAML 路径数对（69 操作），但**内容严重不足**，无法产出可用客户端：

| 维度 | YAML 现状（69 操作） | 客户端生成所需 | 缺口 |
|------|---------------------|---------------|------|
| **requestBody** | 仅 **6** 个操作有（多为 admin 类） | 所有 POST/PUT 的请求体（如 `POST /oauth2/token` 的 grant_type/code/...、`POST /oauth2/login`、`POST /api/register`） | **重**——生成的客户端不知道 `/oauth2/token` 该发哪些表单字段 |
| **response schema** | 仅 **9** 个操作有 | 每个响应的 schema（含错误） | **重**——生成的客户端拿不到强类型响应 |
| **schema 定义** | 仅 **2** 个（`Error`、`TokenResponse`） | TokenRequest、IntrospectionResponse、UserInfoResponse、ClientRegistration、WellKnown 等核心模型 | **重**——生成代码无类型可言 |
| **securitySchemes** | 仅 `bearerAuth`（缺 `clientCredentialsAuth`） | introspect/revoke/token 的 client 凭证（RFC 6749 §2.3 Basic） | **中**——生成的 auth wiring 描述不全 |
| **路径数** | 56（缺 2 个 `/docs/api/*` 自文档） | 见 D1，显式排除 | 轻 |

generated JSON 更糟（71 操作中 **0** 个 requestBody、仅 4 个 response schema）——印证 generated JSON 不能当客户端源，也佐证 YAML 单源方向的正确。

**决策**：**M0 必须含 YAML 内容补齐工作**（不只是删死文件 + 加门）。补齐范围按客户端生成优先级：
1. **P0（阻塞客户端生成）**：补全 6 个核心 OAuth2 端点的 requestBody + response schema + schema 定义——`/oauth2/token`（4 grant 的请求体）、`/oauth2/introspect`、`/oauth2/revoke`、`/oauth2/userinfo`、`/.well-known/openid-configuration`、`/oauth2/login`。补 `clientCredentialsAuth` securityScheme。
2. **P1（覆盖面）**：admin CRUD（`/api/admin/*`）、用户自服务（`/api/me/*`、`/api/register`、`/api/password-reset/*`）。
3. **P2（完整）**：MFA、WebAuthn、social、device 流（这些流程复杂或外部依赖，客户端生成价值低，可标 `deprecated` 或推迟）。
4. **补齐机制**：可结合 #41 的 OpenApiGenerator 修复（让生成器产出更完整的内容），或继续手维护 YAML——**这是 M0 立项时要做的子决策**（本设计文档不预定，留给 M0）。若修生成器，则 D1 的"generated JSON 降级"需重评（生成器修好后 JSON 可能重新成为候选源）。

**与 D3/D4 的关系**：D1.5 补齐**之后**，D3（YAML↔代码一致性门）才有完整意义（现在门只能比 `METHOD path`，比不了 requestBody/schema——那是更深的契约，M0 后置）。D4（oasdiff）依赖 YAML 有 schema 才能检测 schema 收窄——补齐前 oasdiff 只能门路径级破坏性变更。

### D2 — 删除死孤儿 `docs/backend/api/openapi.json`

**依据**：§2.4-1——grep 全仓零引用；唯一被用的是其兄弟 `swagger-ui/` 目录（`apps/server/CMakeLists.txt:76-77` 复制它，但 JSON 本身不复制）。

**决策**：删除 `docs/backend/api/openapi.json`，保留 `docs/backend/api/swagger-ui/`。
**状态**：✅ 已完成（2026-08-12，文件已删；全仓 grep 兜底仍零引用，`swagger-initializer.js` 指向 live 路由 `/docs/api/openapi.json` 不受影响）。
**风险**：低。删除前实施时再跑一次全仓 grep 兜底（确认仍零引用）。`swagger-initializer.js:6` 指向 `/docs/api/openapi.json` 路由（live 生成的，不是这个死文件），不受影响。

### D3 — 引入 YAML↔代码一致性门（**治本设计**）

**依据**：§2.3——指纹测试已硬编码 C++ 的 71 操作权威集（`Property4_OpenApiValidationBaselineTest.cc:90-168`），但它没和 YAML 比对，这是漂移的制度性根因。`.claude/skills/openapi-update` AI 提示词是当前唯一"机制"，而它是给 AI 的提示，不是检查。

**决策**：新增 CI 步骤，从 YAML 提取 `METHOD path` 集，与权威集比对，不一致则 fail：
- **复用** `tools/refactor-baseline/parse_endpoints.py`（已解析 YAML 提取签名）的逻辑，写个轻量 diff 脚本。
- **权威集来源**：指纹测试的 71 操作集，**排除 D1 的例外清单**（`/docs/api/*`，因 YAML 故意不含）。
- **效果**：C++ 加/删/改端点而忘改 YAML → CI 立即报。关闭当前最大治理漏洞。
- **与指纹测试的分工**：指纹测试守护 C++ 侧（端点注册即捕获）；本门守护 YAML 侧（YAML 与 C++ 一致）。两者合力 = 端到端一致性。

### D4 — 引入破坏性变更门（oasdiff）+ SemVer 联动

**依据**：§2.4-3——api-diff 只管 C++ 头，HTTP API 面零守护；客户端 SDK 可发布的前提是破坏性变更有门。

**决策**：
- 用 **oasdiff**（Tufin/oasdiff，Go 单二进制，无 JVM）的 GitHub Action `oasdiff/oasdiff-action`，PR 触发，对比 `apps/server/openapi.yaml` 与 main 分支版本。
- 检测路径/方法删除、参数删除、响应 schema 收窄等（oasdiff 覆盖 509 类破坏性变更）。
- **SemVer 联动**（对齐 api-diff 对 C++ 头的规则）：破坏性变更要求升 major；非破坏性（新增端点/可选参数）可 minor。`oasdiff changelog` 产出按严重度分类的变更日志，支持此判定。
- **版本交叉校验**：`info.version`（YAML）与 `cmake/Version.cmake` 交叉校验，补当前 spec 版本（硬编码 1.0.0）与项目版本脱节的漏洞。
- **lint 层**：保留现有 `openapi-spec-validator`（结构校验）。**Spectral + `owasp:api-security` 规则集**标为**可选**（不强制，避免范围膨胀；未来若需命名/描述一致性/安全规则再加）。

---

## 五、Layer 2：客户端 SDK 生成（在 Layer 1 之上）

### D5 — OAuth2 scheme 保留 `http: bearer/basic`（手写 auth，不引入 `oauth2` flows）

**依据**：
- token 生命周期（refresh 旋转 V008、PKCE、lockout、family 作废）太敏感太变量，不能盲目生成。
- **ZITADEL**：Go SDK 是 gRPC 生成，auth 显式手写包装其 `zitadel/oidc` 库（README 明示 SDK 是低层库的便利包装）。
- **Logto**：每平台轻量手写 SDK，token 管理手写。
- **Ory**：全生成（`ory/sdk` 用 openapi-generator），但其 auth 在服务端（Kratos/Hydra），SDK 是哑 HTTP 客户端，消费者自管 session token——**此模型对 AuthForge（本身是 OAuth2 服务器）不适用**。

**决策**：spec 保留 `http: bearer/basic` scheme；生成器产"请求形态 + 类型"，**auth 手写**（D7）。不引入 `oauth2` flows scheme（会诱导生成器尝试生成 token 生命周期，不可靠）。

### D6 — 生成器选型（按语言，2026 核实）

| 语言 | 选型 | 否决 | 调用方式 |
|------|------|------|---------|
| **Python** | `openapi-python-client`（Pydantic v2 + httpx，原生 3.0/3.1，idiomatic） | openapi-generator 的 legacy `python`（urllib3，非 idiomatic） | `pipx run openapi-python-client`，无 JVM |
| **Go** | `oapi-codegen`（社区标准，net/http/chi/gin，idiomatic） | openapi-generator 的 `go`（interface-heavy，非 idiomatic） | `go install github.com/oapi-codegen/oapi-codegen/cmd/oapi-codegen`，无 JVM |

**JVM 处理**：两者均原生无 JVM，契合 C++ 无 JVM 工具链（避开 `@openapitools/openapi-generator-cli` npm 仍需本地 Java 的坑）。若未来某语言必须用 openapi-generator，用其 **Docker 镜像**（`openapitools/openapi-generator-cli:v7.20.0` pinned）。

### D7 — 手写 auth 层（每语言一个薄包装，作为 blessed 入口）

| 语言 | auth 实现 | 注入方式 |
|------|----------|---------|
| **Go** | `golang.org/x/oauth2/clientcredentials.Config` → `TokenSource`（**自动刷新/缓存/线程安全，不重造**） | `oauth2.NewClient(ctx, ts)` → 经 oapi-codegen 的 `WithHTTPClient(...)` 注入 |
| **Python** | httpx auth flow / transport wrapper，持 client_id/secret，调 `/oauth2/token` client_credentials，注入 Bearer + 到期/401 自动刷新 | 包装 `openapi-python-client` 的 `AuthenticatedClient` |

**覆盖范围**：client_credentials（主，最常用）+ authorization_code（次，浏览器流，客户端库提供 helper 但不全自动化）+ refresh（次）。

### D8 — 仓库布局：monorepo `clients/python` + `clients/go`

与现有 `examples/`、`libs/`、`tests/` 同级。每语言目录：

```
clients/
├── python/
│   ├── generated/          # openapi-python-client 产出，DO NOT EDIT
│   ├── auth/               # 手写 auth 层（httpx transport wrapper）
│   ├── pyproject.toml      # 包元数据，name=authforge
│   ├── README.md
│   └── tests/              # auth 层测试（对本地全栈 target 实测）
└── go/
    ├── generated/          # oapi-codegen 产出，DO NOT EDIT
    ├── auth/               # 手写 auth 层（x/oauth2/clientcredentials）
    ├── go.mod              # module github.com/<org>/authforge/clients/go
    ├── README.md
    └── auth_test.go
```

**不选 separate-repo-per-language**（Ory 模型）：增加 release 协调开销，2 语言不值得；monorepo 与现有 C++ SDK smoke 流程同级，co-located 于生成它们的 spec。

### D9 — 生成代码提交 + 漂移检测门

- **提交生成代码**（带 `// Code generated; DO NOT EDIT`）：release tag 自包含，消费者不需跑生成器。
- **漂移检测门**（CI）：重新生成，与提交的 `generated/` 比对，不一致则 fail——防 stale 生成代码发布。
- **生成节奏**：**release 时生成 + 提交（`[skip ci]`）+ 打 tag + 发布**，不是每 PR 生成（避免评审噪声 + 生成器进每个开发者循环）。
- **工具**：`make gen-sdk`（本地手动）+ CI 漂移门脚本。

### D10 — 发布渠道

| 语言 | 渠道 | 版本 |
|------|------|------|
| Python | PyPI（`pip install authforge`） | 与项目 SemVer 联动（`cmake/Version.cmake`） |
| Go | Go module proxy（`go get github.com/<org>/authforge/clients/go`） | 同上，git tag 驱动 |

挂到现有 `release.yml`（tag 触发）：tag → 生成 clients → 发布。

---

## 六、验收标准（可勾选）

| # | 验收项 | 衡量 |
|---|--------|------|
| ✅ AC1 | 死孤儿删除，全仓零残留引用 | grep 兜底 |
| ✅ AC2 | YAML↔代码一致性门在 CI 生效：故意从 YAML 漏一个端点 → CI fail | CI 故障注入测试 |
| ✅ AC3 | oasdiff 破坏性变更门在 CI 生效：故意删一个路径 → CI fail 且提示要求升 major | 同上 |
| ✅ AC4 | `clients/python` + `clients/go` 各能再生成、漂移门通过、两语言单测绿（工具落点为 `python tools/clients/regen_clients.py [--check]`，§11.5 取代原 `make gen-sdk` 设想） | 本地 + CI（`clients-sdk.yml`），2026-08-18 落实 |
| ✅ AC5 | 每语言 auth 层跑通 client_credentials 流（对本地全栈实测：token → introspect active → discovery → M2M userinfo 401 → 错 secret 401，用例 I1-I5） | `tests/integration`（env 门控）+ 单测矩阵，2026-08-18 落实 |
| ✅ AC6 | 漂移门生效：生成物与 spec 不一致 → `--check` exit 1（regen 脚本 `--selftest` 内含故障注入用例） | selftest + CI |
| ⬜ AC7 | Python 客户端能 `pip install authforge-oauth2` 并发一个 token 请求；Go 客户端能 `go get .../clients/go@vX.Y.Z`（验证 release tag 自包含） | **发布后冒烟**——流水线已接线（release.yml `sdk-python` job + Go 嵌套 tag），待 PyPI 项目注册 + `PYPI_API_TOKEN` secret + 下一 tag（§11.6） |
| ✅ AC8 | **YAML 内容补齐**：6 个核心 OAuth2 端点（`/oauth2/token`、`/introspect`、`/revoke`、`/userinfo`、`/.well-known/openid-configuration`、`/oauth2/login`）有完整 requestBody + response schema；`clientCredentialsAuth` scheme 就位；从 YAML 生成的客户端能发出合法 token 请求（无需手填表单字段） | M0 验收（阻塞 M1） |

---

## 七、实施计划（5 milestone，每里程碑单独立项）

每 milestone 是后续**单独的 `openspec/changes/` 或 `.kiro/specs/` 立项**；本设计文档是它们的共同蓝图。

### M0 — Spec 治理地基（**必须最先**）
- **做**：
  - 删死孤儿（D2）；定 YAML 单源（D1，文档化 + 排除 `/docs/api/*` 例外）。
  - **⚠️ YAML 内容补齐（D1.5，M0 的实质工作量）**：按 P0→P1→P2 优先级补 requestBody / response schema / schema 定义 / `clientCredentialsAuth` scheme。**子决策**：补齐靠手维护 YAML 还是修 `OpenApiGenerator`（#41）生成——立项时定。若修生成器，D1 的"JSON 降级"需重评。
  - 实现 YAML↔代码一致性门（D3）；引入 oasdiff（D4）；可选 Spectral。
- **验收**：AC1/AC2/AC3 + **AC8（YAML 补齐：6 个核心端点有完整 requestBody+response schema，生成的客户端能发出合法 token 请求）**。
- **依赖**：无。**客户端依赖干净且完整单源，故 M0 阻塞 M1–M4。**

### M1 — Python 客户端骨架（单语言打通全链路）
- **做**：`clients/python/` + openapi-python-client 生成 + 手写 httpx auth 层（client_credentials）+ 漂移门 + `make gen-sdk`/`test-sdk`。
- **验收**：AC4/AC5/AC6（Python 版）。
- **依赖**：M0。

### M2 — Go 客户端骨架
- **做**：`clients/go/` + oapi-codegen 生成 + 手写 `x/oauth2/clientcredentials` auth 层 + 漂移门。
- **验收**：AC4/AC5/AC6（Go 版）。
- **依赖**：M0（M1 非依赖，可与 M1 并行）。

### M3 — 发布流水线
- **做**：PyPI 发布（Python）+ Go module proxy 发布（Go）+ `release.yml` 集成（tag → 生成 → 发布）。
- **验收**：AC7。
- **依赖**：M1 + M2。

### M4 — 文档与端到端示例
- **做**：`clients/{python,go}/README.md`；一个端到端示例：用 Python 客户端复现[基准设计](../in-progress/benchmark-facility-design.md)的 S2 client_credentials 场景——闭环"性能数据 + 多语言工具"（附录 D）。
- **验收**：示例可跑。
- **依赖**：M1 + M3（+ 基准设施 M1 数据）。

---

## 八、CI 集成设计（不实现，只设计）

### 8.1 新增 `.github/workflows/openapi-governance.yml`（PR 触发）

jobs：
1. **lint**（保留现状）：`pip install openapi-spec-validator` → `python -m openapi_spec_validator apps/server/openapi.yaml`。
2. **yaml-code-sync**（D3，新）：从 YAML 提取 METHOD path 集，与权威集（指纹测试派生 baseline，排除 `/docs/api/*` 例外）比对，不一致 fail。
3. **breaking-change**（D4，新）：`oasdiff breaking main.yaml PR.yaml`，破坏性变更 fail + SemVer 提示。
4. **client-drift**（D9，新）：重新生成 `clients/python/generated` + `clients/go/generated`，与提交比对，漂移 fail。

### 8.2 挂到现有 `release.yml`（tag 触发）

新增 job：tag → `make gen-sdk` → 提交 `[skip ci]` → 发布 PyPI / Go module proxy。

---

## 九、风险与缓解

| 风险 | 等级 | 影响 | 缓解 |
|------|------|------|------|
| **⚠️ YAML 内容稀疏（D1.5）**：当前 69 操作仅 6 有 requestBody / 9 有 response schema，无法直接生成可用客户端 | **高** | M0 工作量被低估；客户端生成质量取决于补齐程度 | M0 必须含 YAML 补齐（AC8）；按 P0→P1→P2 优先级；补齐靠手维护还是修生成器（#41）由 M0 立项决策 |
| **YAML↔代码一致性门的例外清单（`/docs/api/*`）漂移** | 中 | 例外清单本身要维护 | 文档化排除规则 + 门里硬编码例外清单 + 注释说明为何排除 |
| **oasdiff 误报**（纯描述变更被判破坏性） | 中 | 门噪扰 | 配置只门破坏性类（509 规则），非破坏性仅警告或 info。⚠️ 补齐前 oasdiff 只能门路径级（schema 级需 YAML 补齐 schema 后才有意义） |
| **生成器版本升级致生成代码大 diff** | 低 | 评审噪声 + 潜在不兼容 | pin 生成器版本（`openapi-python-client==X.Y.Z`、`oapi-codegen vX.Y.Z`）；升级走独立 PR |
| **auth 层与生成代码耦合**（API 升级时 auth 包装要同步） | 中 | 升级时 auth 层失效 | auth 层只依赖稳定的请求形态（端点路径 + 参数 schema），不依赖生成代码内部结构；API 升级由 oasdiff 门预警 |
| **OpenApiGenerator 的 security object bug + 缺 clientCredentialsAuth**（`OpenApiGenerator.cc:195-200, 122-128`，已提 [#41](https://github.com/lucaswang420/authforge/issues/41)） | 低 | generated JSON 畸形 + scheme 不全 | 本设计不改它（N1）；YAML 单源后 generated JSON 只供 Swagger UI，畸形 security 不阻断 UI 渲染。**#41 跟踪修复**；若 M0 选择修生成器补内容（D1.5 子决策），则 #41 一并修，且 D1 的"JSON 降级"需重评 |
| **`info.version` 与项目版本脱节**（当前都硬编码 1.0.0） | 低 | 版本承诺混乱 | D4 的版本交叉校验补此；release 时同步 `info.version` 与 `cmake/Version.cmake` |
| **client_credentials 在 memory 模式不可达**（无 `backend-svc`） | 中 | AC5 实测需全栈 | auth 层测试用基准设计的 postgres+redis 全栈 target，非 memory 模式（同基准设计 D1） |

---

## 附录 A：三 OpenAPI 文件核实对比表

| 文件 | 路径 | 路径数 | 来源 | CI 校验 | 消费方 | stale | 处置 |
|------|------|--------|------|---------|--------|-------|------|
| generated JSON | `apps/server/docs/api/openapi.json` | 58 | `OpenApiGenerator.cc` 启动生成 | 否（指纹测试间接） | Swagger UI | 当前 | **降级**为派生产物（D1） |
| 手维护 YAML | `apps/server/openapi.yaml` | 56 | 人工编辑 | 是（`openapi-spec-validator`） | 端点签名门 + 客户端生成 | 当前 | **唯一权威**（D1） |
| 死孤儿 JSON | `docs/backend/api/openapi.json` | 8 | generated JSON 旧快照 | 否 | **零引用** | 3 月前 | ✅ **已删除**（D2，2026-08-12） |

---

## 附录 B：71 操作权威集来源

指纹测试 `tests/integration/concurrency/Property4_OpenApiValidationBaselineTest.cc:90-168`：
- 链接全部 controller，调 `OpenApiGenerator::generateOpenApiSpec()`，断言排序后 `METHOD path` 集（71 行）逐字节等于硬编码串。
- 含 `GET /docs/api/openapi.json`（line 124）——YAML 缺的 2 自文档路径之一。
- **此集 = C++ 代码真实 API 面，权威且经测试**。D3 的一致性门以它（排除 `/docs/api/*` 例外）为基准。

---

## 附录 C：客户端目录结构设计（仅设计，不创建）

见 §五 D8 的 `clients/` 布局。两语言对称：`generated/`（DO NOT EDIT）+ `auth/`（手写）+ 包元数据 + README + tests。

---

## 附录 D：与基准设施设计的协同

[基准设施设计](../in-progress/benchmark-facility-design.md)的 Phase 0 产出可复现性能数据，但第三方复现需要"数据 + 工具"齐备：
- 基准设施给 wrk 脚本（C++ 开发者/性能工程师用）。
- **本设施的 clients/ auth 层给非 C++ 开发者用**——Python/Go 开发者可用客户端发同样的请求，复现 S2 client_credentials 等场景，不必学 wrk。
- M4 的端到端示例正是此协同的闭环：Python 客户端复现基准 S2 场景。

```
Phase 0 基准设施（wrk）──性能数据──┐
                                  │ 两类读者
Phase 1 客户端 SDK（py/go）──请求工具──┘ C++ 工程师(wrk) + 非 C++ 开发者(客户端)
```

---

## 附录 E：与上游文档的关系

| 上游 | 关系 |
|------|------|
| [调研报告](../productization-research.md) §3.2「API 稳定性」卖点 | 本设施 D4（oasdiff）补其工程证据 |
| [调研报告](../productization-research.md) §3.3 差距「多语言 SDK」 | 本设施是此差距的填补 |
| [演进方案](../productization-evolution-plan.md) §三 Phase 1 | 本设施是 Phase 1 的落地设计 |
| [基准设施设计](../in-progress/benchmark-facility-design.md) | 姊妹设计，附录 D 协同 |

---

*本设计基于代码库 v1.0.0 现状（2026-08-05 核实）+ 2026 年生成器生态调研编制。实施为后续 5 个 milestone（M0 必须最先），本文档是它们的共同蓝图与约束源。Layer 1（spec 治理）是 Layer 2（客户端生成）的承重前提——不先治 spec 就生成客户端等于在流沙上盖楼。*

---

## 十、M0 立项修订（2026-08-16 实测，v1.1）

> M0 启动前的实地核查修正了 §二 的若干判断，并落地了 D1.5 遗留子决策。
> 实施计划（带验收标准）见 [openapi-spec-governance-plan.md](../todo/openapi-spec-governance-plan.md)。

### 10.1 三层漂移实测（比 §二 更严重：不止两源，是三层）

对 **Drogon 路由**（`ADD_METHOD_TO` 宏全集，82 操作）、**C++ 文档注册面**（指纹测试基线，75 操作）、
**YAML**（73 操作）做三方对比（2026-08-16，master `e5548ec`）：

| 层 | 数量 | 发现 |
|----|------|------|
| 路由（真实 API 面） | 82 | 12 条路由从未注册文档、从未入 YAML |
| C++ 文档注册（generated JSON / 指纹） | 75 | **5 条是幽灵文档**（注册了不存在的路径） |
| 手维护 YAML | 73 | **8 条死条目**（无路由支撑）+ 缺 13 条真实操作 |

具体清单：

**① YAML 死条目（8 操作 / 6 路径，无路由支撑）——删除**：
- `/api/orgs` GET/POST、`/api/orgs/{orgId}/users` POST —— 路由早已删除（`OrganizationController.cc:20-21` 注释明言"dead /api/orgs endpoints"），YAML 未跟上
- `/oauth2/device/verify` GET/POST —— 无路由；设备验证页由前端渲染
- `/oauth2/mfa/setup`、`/oauth2/mfa/setup/verify`、`/oauth2/mfa/disable` —— 旧路径；真实路由已迁至 `/api/me/mfa/*`（`MfaController.h:52-72`）

**② C++ 幽灵文档（5 操作，注册了不存在路径）——修正**：
- `/oauth2/device/verify` GET/POST、`/oauth2/mfa/setup|setup/verify|disable` —— 同上，`OpenApiGenerator::addEndpoint` 注册的路径与 `ADD_METHOD_TO` 脱节

**③ 真实路由但文档+YAML 双缺（8 操作）——补注册 + 补 YAML**：
- `/oauth2/logout` POST（SessionController）
- `/api/me/mfa/setup|verify|disable` POST ×3（MfaController 新路径）
- `/oauth2/device/approve` POST（admin 授权，DeviceAuthController）
- `/health/live`、`/health/ready` GET（HealthController）
- `/.well-known/oauth-authorization-server` GET（RFC 8414，DiscoveryController——**对客户端 SDK 有直接价值**）

**③b 真实路由、YAML 已有、文档注册缺（2 操作）——仅补注册**：
- `/oauth2/end_session` GET+POST —— YAML 已完整定义（`openapi.yaml` `end_session` 条目），但 C++ 侧未注册文档

**④ YAML 缺但 C++ 文档已有（5 操作）——补进 YAML**：
- `/api/admin/dashboard` GET、`/api/admin/organizations` GET+POST、`/api/admin/organizations/{slug}` GET、`/api/admin/scopes/resources` GET

**⑤ 有意不文档化（例外清单）**：`GET /login`（服务端渲染 HTML 页，非 API）、`GET /docs/api`（自文档跳转）。

对账后集合（推演，以门脚本实跑为准）：文档注册面 = 75 − 5 + 10 = **80**；
YAML = 73 − 8 + 13 = **78**。

> §二.3 的判断"指纹测试 = C++ 代码真实注册的 API 面，权威且经测试"需要修正：
> 指纹只守护**文档注册面**，不守护**路由面**。路由可以存在而无文档（上述 11 条），文档也可以指向不存在的路由（上述 5 条幽灵）。**一致性门必须同时比对路由↔文档↔YAML 三层**（见 10.3）。

### 10.2 D1.5 子决策落地：手维护 YAML（不修生成器）

M0 立项时按 §四 D1.5 的要求裁决：**补齐机制 = 手维护 YAML**。

理由：
1. `OpenApiGenerator` 的 `EndpointInfo` 结构根本不承载 requestBody/response schema（generated JSON 71 操作 0 requestBody）——修生成器到 schema 级保真度是大工程，而 D1 已把 generated JSON 降级为 Swagger UI 派生产物，ROI 不成立。
2. YAML 是 CI lint + 一致性门 + 将来客户端生成的输入，直接对照控制器实测契约（M0 已产出 6 个核心端点的逐字段 wire 契约核查，见实施计划附录）手工补齐，可评审、可增量。
3. #41（生成器 security object bug + 缺 clientCredentialsAuth）维持 N1 排除，不随 M0 修。

### 10.3 D3 修正：一致性门升级为三层比对

原设计只比对 YAML ↔ 指纹。10.1 证明指纹本身会漂移（幽灵文档/漏文档），门升级为：

```
路由集（源码扫描 ADD_METHOD_TO/METHOD_ADD，无需编译）
  ↔ 文档注册集（解析指纹测试基线字符串，单一源不复制）
  ↔ YAML 操作集（解析 openapi.yaml）
```

两条断言 + 显式例外清单（`GET /login`、`GET /docs/api` 路由级排除；`GET /docs/api/`、`GET /docs/api/openapi.json` YAML 级排除——自文档端点对 SDK 无意义，维持 D1 决策）。解析失败有数量下限兜底（≥60 操作），防止门静默失效。

### 10.4 D4 补充：bootstrap 豁免与版本联动落点

- **oasdiff 首跑豁免**：M0 的破坏性变更不止 8 条死条目删除——P0 端点把误标的 query 参数迁移为 requestBody（如 introspect 的 `token` 从 required query 迁走）、安全声明收紧，oasdiff 同样判 breaking。首跑豁免清单 = **8 条死端点删除 + P0 端点参数/安全声明迁移**，逐条带理由与关联 commit；此后任何新的 breaking 变更必须走 major 升级或显式豁免。
- **`info.version` 联动落点**：YAML `info.version` 与 `cmake/Version.cmake` 交叉校验进一致性门脚本（当前 YAML 硬编码 1.0.0，项目已 1.2.0——脱节实锤）；`release.yml` 的 version-check job 增加同校验（tag 发布时兜底）。
- **oasdiff 运行方式**：直接下载官方 release 二进制（Go 单文件，无 JVM），不用 wrapper action，便于本地复现。

### 10.5 AC 修订

§六 AC2 的验收方式细化为**本地故障注入**（从 YAML 副本删一个端点 → 门 fail；往源码副本加一条路由 → 门 fail），CI 侧由本 PR 自身触发新 workflow 验证。AC8 的"完整 requestBody + response schema"以控制器实测契约为准（M0 已核查到 file:line 级），不再以"YAML 里有内容"为满足。

---

## 十一、M1/M2 立项修订（2026-08-18 实测，v1.2）

> M1（Python）/M2（Go）启动前的实地核查：生成器从治理后的 `apps/server/openapi.yaml`
> 实际生成 + 编译验证，落地 D7/D8/D9/D10 的遗留子决策。实施计划（带验收标准）见
> [client-sdk-implementation-plan.md](client-sdk-implementation-plan.md)。

### 11.1 生成器实测与版本 pin（D6 修正）

2026-08-18 在 master `9c13b41`（v1.2.0，M0 已合并）上实测：

| 生成器 | pin 版本 | 实测结果 |
|--------|----------|----------|
| `openapi-python-client` | **0.29.0** | 从 YAML 全量生成 156 个 .py，零错误零警告；`TokenRequest`/`TokenResponse` 等模型类型完整（grant_type 枚举、UNSET 可选字段、表单 `to_dict()`） |
| `oapi-codegen` | **v2.8.0**（runtime `v1.7.0`） | 生成 14471 行 `client.gen.go`，`go build` + `go vet` 通过 |

- **D6 描述修正**：openapi-python-client ≥0.24 已从 Pydantic 迁移到 **attrs** 模型（本设计 v1.0 写"Pydantic v2 + httpx"已过时）。功能等价：类型化、可序列化、py.typed。依赖：`httpx >=0.23.1,<0.29.0`、`attrs >=22.2.0`、Python `^3.11`。
- Go 工具链：`go 1.24` 指令；生成器经 `go run ...@v2.8.0` 调用（无需全局安装）。本地网络注意：模块下载需 GOPROXY 镜像（如 goproxy.cn），CI（GitHub runner）不受影响。

### 11.2 D10 修正：PyPI 发行名 = `authforge-oauth2`（导入名保持 `authforge`）

实测 pypi.org：**`authforge`（0.1.0，无关的 FastAPI 认证库）与 `authforge-sdk`（1.0.8，另一家 "AuthForge" 产品的官方 SDK）均已被占用**——D10 原文"`pip install authforge`"不可行。

决策：**发行名（distribution name）= `authforge-oauth2`，导入名（import name）= `authforge`**。发行名/导入名分离是 PyPI 常规模式（如 `protobuf` → `google.protobuf`）。用户体验：

```bash
pip install authforge-oauth2     # 安装
from authforge import m2m_client  # 导入
```

Go module path 不受影响：`github.com/lucaswang420/authforge/clients/go`（monorepo 子目录模块，git tag 驱动，无命名冲突）。

### 11.3 Python 布局定稿（D8 细化）

```
clients/python/
├── pyproject.toml                    # 发行名 authforge-oauth2；version 与 cmake/Version.cmake 联动（CI 校验）
├── openapi-python-client.yaml        # 生成器配置（package_name_override: authforge）
├── README.md
├── examples/client_credentials_demo.py
├── src/authforge/
│   ├── __init__.py                   # 公共 API 出口（m2m_client 等）
│   ├── m2m.py                        # client_credentials 门面（httpx.Auth + 生成客户端组装）
│   ├── oauth.py                      # authorization_code helper（authorize URL + PKCE + code 交换 + refresh）
│   └── generated/                    # 生成代码（DO NOT EDIT）
└── tests/
    ├── test_m2m.py 等                # 单测（httpx.MockTransport，CI 可跑）
    └── integration/                  # AUTHFORGE_BASE_URL 门控，对本地全栈实测
```

两个实测约束决定了 regen 脚本的搬运逻辑：

1. **点分 `package_name_override` 不可用**（会创建字面量目录 `authforge.generated/` 而非嵌套包）；但 0.29.0 生成代码全部使用**包内相对导入**（`from ...client import ...`），故"生成到临时目录（包名 `authforge`）→ 只搬包目录本体到 `src/authforge/generated/`"可行，包可安全重定位。
2. 生成器会在输出目录额外写 `pyproject.toml`/`README.md`/`.gitignore` 等项目级文件——regen 脚本只搬运 `<pkg>/` 目录，项目元数据以我们手维护的为准。

### 11.4 auth 注入缝定稿（D7 细化，以生成代码实测为准）

| 语言 | 注入缝 | 说明 |
|------|--------|------|
| Python | 生成 `Client`/`AuthenticatedClient` 的 `set_httpx_client()` / `set_async_httpx_client()` | 构造 `httpx.Client(base_url=..., auth=ClientCredentialsAuth(...))` 后注入；注入路径绕开 `AuthenticatedClient` 的静态 token header 构造。secured 端点函数签名**类型窄化为 `AuthenticatedClient`**，故门面返回注入完成的 `AuthenticatedClient` |
| Go | `generated.NewClient(serverURL, generated.WithHTTPClient(oauth2Client))` | `golang.org/x/oauth2/clientcredentials.Config` → `TokenSource`（自动刷新/缓存/并发安全）；**`AuthStyle: AuthStyleInHeader`**——F-017 要求 client_secret_basic（body 形态被服务端拒绝） |

token 生命周期职责（D5 不变）：**client_credentials 全自动**（到期前主动刷新 + 401 时强制刷新重试一次）；**authorization_code / refresh 提供 helper**（authorize URL 构造、PKCE S256、code 交换、refresh 调用），不做浏览器自动化。

### 11.5 D9 修正：漂移门落点与生成节奏

- **漂移门不放 `ci.yml` static-checks**（该 job 定位 source-only、无工具链依赖）；新增独立 workflow **`clients-sdk.yml`**（PR 触发，paths: `clients/**`、`apps/server/openapi.yaml`、`tools/clients/**`）：
  1. `regen_clients.py --check`（Python + Go 生成物对账，漂移即 fail）
  2. Python 单测（pytest）+ Go 单测（`go test ./...`，集成测试 env 门控自动跳过）
  3. 版本联动检查：`clients/python/pyproject.toml` version == `cmake/Version.cmake`
- **生成节奏修正**：PR 提交生成物 + CI 漂移门对账（放弃 D9 原文"release 时生成 + `[skip ci]` 提交"——漂移门已在每个 PR 保证新鲜度，release 再生成引入移动部件无安全增益；D4"生成代码提交进 git"主决策不变）。
- 工具：`python tools/clients/regen_clients.py [--check]`（跨平台单脚本，pin 生成器版本常量）。**不新增 manage.ps1/manage.sh 命令**——避免 parity 面扩大，repo 惯例的工具脚本形态（`tools/<name>/<name>.py`）已够用。

### 11.6 M3 范围修正（发布流水线）

- **Go module proxy：需要嵌套 tag，不是零工作**——子目录模块的版本解析要求形如 `clients/go/vX.Y.Z` 的 git tag（go.dev/ref/mod#vcs-version；根 tag `vX.Y.Z` 只服务仓库根模块，`go get .../clients/go@v1.2.0` 对根 tag 会报 unknown revision）。`release.yml` 的 `github-release` job 增加一步：在发布 commit 上创建并推送 `clients/go/v${VERSION}` 嵌套 tag（该形态不匹配 release.yml 的 tag 触发模式 `v[0-9]+.[0-9]+.[0-9]+`，无递归触发）。**另注**：module path 无 `/vN` 后缀 ⇒ 只能消费 v0/v1 tag；项目升 v2.0.0 时须同步把 module path 改为 `.../clients/go/v2`（README 记录此约束）。
- **PyPI**：`release.yml` 新增 `sdk-python` job（tag 触发：**版本一致性前置校验**（pyproject `version` == tag 版本，防漏 bump 发布错版）→ `python -m build clients/python` → `pypa/gh-action-pypublish`）。secret 门控语义：GitHub Actions 的 `secrets` context **不能**用于 job 级 `if:`（静默求值为不可用）——正确写法是把 `PYPI_API_TOKEN` 绑到 job 级 `env:`，在 **publish step** 上 `if: env.PYPI_API_TOKEN != ''`；未配置时该 step 显式 skip 并打印设置指引，**不阻塞既有发布链**。首次发布前置（人工，一次性）：在 PyPI 注册 `authforge-oauth2` 项目 + 仓库配 secret——本 PR 的 README/发布文档列出步骤。
- **AC7 拆分**：`pip install authforge-oauth2` / `go get .../clients/go@vX.Y.Z` 的发布后冒烟 = **发布验收，不在本 PR**；本 PR 验收 = 流水线 wiring + workflow 语法/dry-run 验证。

### 11.7 测试策略

| 层 | 范围 | 跑在哪 |
|----|------|--------|
| 单测 | token 获取（Basic 认证形态）/ Bearer 注入 / 到期前主动刷新 / 401 刷新重试一次 / 错误传播 / PKCE S256 向量 | CI（`clients-sdk.yml`）：Python `httpx.MockTransport`，Go `httptest` |
| 集成 | client_credentials 取 token → introspect `active:true` → discovery 拉取 →（负例）M2M token 调 userinfo 得 401 | 本地全栈（复用 full-test 基建：PG + 服务器 :5555 + 种子 `backend-svc`/`test-secret`）；`AUTHFORGE_BASE_URL` env 门控，CI 不跑 |
| 回归 | 全量后端 8 步 + 前端测试 | 本任务不改 C++/前端源码、不改 openapi.yaml，预期零影响，仍全量跑一遍兜底 |
