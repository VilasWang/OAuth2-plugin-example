# OpenAPI Spec 治理实施计划（M0）

> **日期**: 2026-08-16
> **上游设计**: [client-sdk-facility-design.md](../in-progress/client-sdk-facility-design.md) §四（Layer 1）+ §十（M0 立项修订）
> **上游任务**: [next-phase-implementation-plan.md](../next-phase-implementation-plan.md) A1
> **基线**: master `e5548ec`（v1.2.0）
> **范围**: M0 = D1.5 schema 补齐 + 三层端点对账 + D3 一致性门 + D4 oasdiff 破坏性变更门。M1+（客户端生成）不在本计划。

---

## 一、交付物总览

| # | 交付物 | 类型 |
|---|--------|------|
| W1 | `apps/server/openapi.yaml` 重写：端点对账（-8 死条目 / +13 缺失）+ P0 schema 补齐 + `info.version: 1.2.0` | spec |
| W2 | C++ 文档注册修正（5 处幽灵路径修正/删除 + 10 处缺失补注册），指纹测试基线同步更新 | C++ |
| W3 | `tools/openapi-governance/check_spec_governance.py`：三层一致性门 + 版本交叉校验（带 selftest） | 工具 |
| W4 | CI 接线：`ci.yml` static-checks 加一致性门步骤；新增 `.github/workflows/openapi-governance.yml`（oasdiff breaking 门） | CI |
| W5 | oasdiff 首跑豁免清单（9 条死端点删除的一次性 errata） | 配置 |
| W6 | `tools/refactor-baseline/endpoints/openapi.signature.txt` 再生（手动 refactor 门保持连贯） | 基线 |
| W7 | 验收证据：openapi-python-client 生成的客户端实调 `/oauth2/token` + `/oauth2/introspect` 成功 | 验收 |

---

## 二、W2 — C++ 文档注册修正（先做，因为它定义权威集）

原则：**文档注册路径必须与 `ADD_METHOD_TO` 路由逐字相等**；真实路由必须注册文档（例外清单除外）。

| 文件 | 改动 |
|------|------|
| `libs/drogon/src/controllers/MfaController.cc` | 3 条文档路径改名：`/oauth2/mfa/setup`→`/api/me/mfa/setup`、`/oauth2/mfa/setup/verify`→`/api/me/mfa/verify`、`/oauth2/mfa/disable`→`/api/me/mfa/disable`（`/oauth2/mfa/verify` 保留） |
| `libs/drogon/src/controllers/DeviceAuthController.cc` | 删除 `/oauth2/device/verify` GET/POST 幽灵文档；新增 `/oauth2/device/approve` POST 文档（admin，AuthorizationFilter） |
| `libs/drogon/src/controllers/SessionController.cc` | 新增 `/oauth2/end_session` GET+POST（YAML 已有、仅缺注册）、`/oauth2/logout` POST 文档 |
| `libs/drogon/src/controllers/HealthController.cc`（注意：该文件现无文档注册机制，`GET /health` 的注册在 SessionController.cc 的 static-ctor 结构里） | 新增 `/health/live`、`/health/ready` GET 文档——用 static-ctor 自注册惯例（服务器与测试二进制都链接到，指纹测试才能看到），避免 initApiDocs 双接线（main.cc + tests/test_main.cc） |
| `libs/drogon/src/controllers/DiscoveryController.cc` | 新增 `/.well-known/oauth-authorization-server` GET 文档（RFC 8414） |
| `tests/integration/concurrency/Property4_OpenApiValidationBaselineTest.cc` | `kFingerprint` 基线更新为对账后集合，并在文件头加注释：此基线被 `tools/openapi-governance/check_spec_governance.py` 解析，改动需同步 |

对账后权威集（文档注册面）= 现 75 − 5 幽灵 + 10 新注册 = **80 操作**；
YAML = 80 − 2 自文档排除（`/docs/api/`、`/docs/api/openapi.json`）= **78 操作**；
路由 82 − `GET /login` − `GET /docs/api`（路由级排除）= 80。

> ⚠️ 数字以实施时脚本实跑为准（上式为推演）；门脚本断言的是**集合相等**而非数量。

**例外清单（硬编码进门脚本，带理由注释）**：
- 路由级（有路由、永不文档化）：`GET /login`（HTML 页面）、`GET /docs/api`（自文档跳转变体）
- YAML 级（有文档注册、不入 YAML）：`GET /docs/api/`、`GET /docs/api/openapi.json`（SDK 无意义的自文档端点，D1 决策）

**验收 W2**：
- [ ] Windows 本地构建（`./manage.ps1 build`）+ `ctest -R OpenApiSpec_Property4`（指纹测试）绿
- [ ] 服务器可启动，`/docs/api/openapi.json`（generated JSON）与新文档注册面一致

## 三、W1 — YAML 重写（核心工作量）

### 3.1 端点层对账

- 删除 8 条死条目（§十.10.1 ①）
- 新增 13 条缺失：§十.10.1 ③ 的 8 条 + ④ 的 5 条（③b 的 end_session 已在 YAML，仅 W2 补注册）
- `/oauth2/mfa/verify`、`/oauth2/consent`、`/oauth2/authorize` 等保留条目中把"参数在 query"的误导性声明改为真实位置（见 3.2 契约表）

### 3.2 P0 schema 补齐（以 2026-08-16 控制器实测契约为准）

新增 `components/schemas`（全部从实测契约生成，file:line 依据存于本计划附录 A）：

| Schema | 要点 |
|--------|------|
| `OAuth2Error` | RFC 6749 §5.2：`error`(enum) + `error_description?` + `error_uri?`；`/oauth2/*`、`/.well-known/*` 端点的错误体 |
| `ErrorEnvelope` / `Error` | 应用端点错误：`{"error": {category, code, message, request_id, numeric_code?, details?}}`——修正现有 `Error` schema：补 `numeric_code`（`details` 字段已有，核对类型即可）+ 建模外层 `error` 包装 |
| `TokenResponse` | 单 schema + 可选字段（access_token, token_type, expires_in, refresh_token?, scope?, roles?, id_token?），描述标注各 grant 的字段出现规则（避免 oneOf 加重生成客户端） |
| `IntrospectionResponse` | `active` + client_id?/token_type?/exp?/iat?/nbf?/sub?/aud?/iss?/scope?；inactive 时仅 `{active: false}` |
| `UserInfoResponse` | sub, name, username?, email?, email_verified?, roles? |
| `OpenIDConfiguration` | discovery 全字段（issuer…claims_supported，含 end_session_endpoint、backchannel_logout_supported） |
| `OAuthAuthorizationServerMetadata` | RFC 8414 全字段（无 userinfo_endpoint/jwks_uri） |
| `JWKSet` | `{keys: [{kty, kid?, use?, alg?, n?, e?, x5c?}]}` |
| `LoginSuccess` / `MfaRequiredResponse` | login `json=true` 的 `{code, location}`；`{mfa_required, mfa_token, message}` |
| `MessageResponse` | `{message}`（logout/end_session/通用） |
| `DeviceAuthorizationResponse` | device_code, user_code, verification_uri, expires_in, interval（**无** verification_uri_complete） |
| `HealthStatus` | health/live/ready 的状态体 |

P0 端点的 requestBody 修正（当前 YAML 把 token/introspect/revoke/login 参数全标 `in: query`，与实现不符）：

| 端点 | 真实请求形态 |
|------|--------------|
| `POST /oauth2/token` | `application/x-www-form-urlencoded` body（getParameter = query **或** form；RFC 6749 要求 form）+ HTTP Basic 客户端认证可选。requestBody 用 form schema（grant_type enum 4 值、code、redirect_uri、refresh_token、client_id、client_secret、scope、code_verifier、device_code），同时保留 query 参数声明会误导生成器——**改为 requestBody 为主，query 不声明**，描述注明 Drogon 兼容 query |
| `POST /oauth2/introspect` | 同上：form body `token` + `token_type_hint?`；客户端凭证 Basic（必须） |
| `POST /oauth2/revoke` | form body `token` + `token_type_hint?`；Basic（confidential 必须，public 豁免） |
| `POST /oauth2/login` | JSON body **或** form/query 双形态（SessionController 分支读 jsonObject）；requestBody 声明 `application/json` + `application/x-www-form-urlencoded` 两种 content |
| `GET /.well-known/*`、`GET /oauth2/userinfo` | 无 body；userinfo 补全 200/401/403 响应 schema |

安全声明修正：token = 无强制 scheme（grant 依赖）；introspect = `clientCredentialsAuth`（必须）；revoke = `clientCredentialsAuth` 可选（public 豁免，`security: [{clientCredentialsAuth: []}, {}]`）。

P1（admin/self-service 响应 schema）与 P2（WebAuthn/social 等）维持现状路径级描述——本 PR 只对账端点，不承诺全量 schema（C1 Python 客户端主要消费 P0）。

`info.version`: 1.0.0 → **1.2.0**（与 `cmake/Version.cmake` 对齐，进 W3 校验）。

**验收 W1**：
- [ ] `python -m openapi_spec_validator apps/server/openapi.yaml` 绿
- [ ] 三层一致性门（W3）报告 YAML ↔ 文档注册面差异 = 0（除例外清单）
- [ ] 6 个 P0 端点均有 requestBody（如适用）+ 主要响应的 `content.application/json.schema.$ref`

## 四、W3 — 一致性门脚本 `tools/openapi-governance/check_spec_governance.py`

检查（单脚本三检查，一次 CI 步骤）：

1. **routes ↔ docs**：源码扫描 `ADD_METHOD_TO`（多行宏需跨行匹配；本仓库无 `METHOD_ADD`）覆盖 `libs/drogon/include/.../controllers/*.h` + `apps/server/**`，提取 `METHOD path` 集——每个宏只取第一个方法 token（GitHub/Google/WeChat 的 `::drogon::Post, ::drogon::Options` 双方法声明只取主方法，OPTIONS 忽略）；解析指纹测试 `kFingerprint` 字符串提取文档注册集；断言 `docs == routes − ROUTE_ONLY`。
2. **docs ↔ yaml**：解析 `openapi.yaml` paths；断言 `yaml_ops == docs − YAML_EXCLUDED`。
3. **version sync**：`openapi.yaml` `info.version` == `cmake/Version.cmake` 的 `FULLA_PROJECT_VERSION`。

工程约束：
- 遵循仓库脚本惯例：`--selftest`（fixture 驱动）、退出码 0/1/2、ASCII 输出
- 复用 `tools/refactor-baseline/parse_endpoints.py` 的 YAML 加载（PyYAML 优先，MiniYaml 兜底）。CI 上 `pip install openapi-spec-validator` 已连带装 PyYAML，但为确定性在新步骤的 pip 行显式加 `pyyaml`；MiniYaml 仅作本地兜底（对本门只读 paths/methods/version 的用途足够）
- 解析兜底：指纹提取数 < 60 或路由提取数 < 60 → 按 env 错误 fail（防门静默失效）
- 例外清单常量 + 每条一行理由注释

**验收 W3**：
- [ ] `python tools/openapi-governance/check_spec_governance.py` 在对账后的树上退出 0
- [ ] `--selftest` 绿
- [ ] 故障注入 ①：YAML 副本删一个端点 → 退出 1 且 diff 输出指名该端点
- [ ] 故障注入 ②：源码副本（fixture）加一条路由 → 退出 1
- [ ] 故障注入 ③：YAML `info.version` 改错 → 退出 1

## 五、W4/W5 — CI 接线 + oasdiff 门

### 5.1 `ci.yml` static-checks 追加一步

```yaml
- name: Check OpenAPI Spec Governance (3-layer consistency + version sync)
  run: python3 tools/openapi-governance/check_spec_governance.py
```

纯源码扫描，无新依赖，符合 static-checks"source-only"定位。

### 5.2 新增 `.github/workflows/openapi-governance.yml`（PR 触发）

- job `breaking-change`：checkout base（`origin/master`）+ head；下载 pinned oasdiff release 二进制（linux-amd64，版本 pin，SHA 校验可选）；`oasdiff breaking base/apps/server/openapi.yaml head/apps/server/openapi.yaml`；非空 breaking 集 → fail，输出要求"升 major 或更新豁免清单"。
- 豁免清单：`tools/openapi-governance/oasdiff-breaking-ignore.yaml`（机制以 oasdiff 当前版本文档为准——ignore-file/errata，实施时核实）。首跑豁免 = **8 条死端点删除 + P0 端点参数/安全声明迁移**（query→requestBody 迁移、introspect `token` required query 移除、security 收紧——这些同样是 oasdiff 判定的 breaking），逐条带理由与关联 commit。
- 该 workflow 同时承担"门本身活着"的验证：本 PR 触发首跑，豁免清单必须恰好覆盖 M0 的破坏性变更（死端点删除 + P0 参数迁移）——多了放水、少了误杀，都算验收失败。

### 5.3 `release.yml` version-check 追加一行

`version-check` job 增加 YAML `info.version` == `cmake/Version.cmake` 校验（tag 发布兜底；PR 侧已由 W3 门覆盖，此处防 release 路径漂移）。
- [ ] 改动后 YAML 版本与 tag 不一致时 version-check 失败（推演即可，不强求实跑 tag）

**验收 W4/W5**：
- [ ] 本 PR 上 `openapi-governance.yml` 绿（证明豁免清单恰好覆盖 bootstrap 删除）
- [ ] 本 PR 上 static-checks 新步骤绿
- [ ] （门有效性）本地用 oasdiff 二进制对"删除一个存活路径"的副本 spec 实跑 → 报 breaking（AC3 故障注入）

## 六、W6 — refactor-baseline 签名再生

`python tools/diff-endpoint-baseline.py --update-baseline`（statuses/content-types 因 schema 补齐大量变化）；该门仅手动 capture 流程消费，不在 CI，但保持连贯避免下次 refactor 误报。**注**：该基线在 master 上已先行漂移（69 行 vs YAML 73 操作）——本次再生一并收敛，commit 信息注明含存量漂移。

- [ ] 更新后再跑 `diff-endpoint-baseline.py`（diff 模式）退出 0

## 七、W7 — AC8 验收：生成客户端实调

步骤（Windows 本机，PG13 native + Release 构建，构建/启停用 `./manage.ps1` 入口，参照 full-backend-test 基建）：
1. `pipx run openapi-python-client generate --url file://.../openapi.yaml --output .tmp-acceptance/client`（或 `--path`）
2. 启动服务器（postgres 模式），用端点测试同款种子客户端凭证
3. 脚本：生成客户端发起 `client_credentials` token 请求 → 拿 access_token；用 client 凭证 introspect 该 token → `active: true`
4. 断言：两个调用均由**生成代码**构造请求体（表单字段来自 schema，无需手拼 form）

- [ ] token 200 + `access_token` 非空；introspect 200 + `active == true`
- [ ] 生成器无因 spec 缺陷导致的警告性跳过（如缺 schema 退化为 Any）

## 八、文档同步（实施后）

| 文档 | 改动 |
|------|------|
| `.claude/skills/openapi-update/SKILL.md`（+ `.zcode`/`.codebuddy`/`.qoder` 镜像） | 工作流加第 5 步：跑治理门脚本；提示 MFA 新路径、版本联动 |
| `docs/backend/api-reference.md` | 对照对账结果核对（尤其 /api/orgs 死条目、mfa 路径、新增 11 端点） |
| `next-phase-implementation-plan.md` | A1 状态更新 |
| `progress-status.md` | 加进度行 |
| `docs/backend/ci-cd-guide.md` | 补 openapi-governance workflow 说明（若该文档覆盖 CI 清单） |

## 九、实施顺序与提交切分

```
1. C++ 文档注册修正 + 指纹基线更新（W2）        → commit 1（可独立构建验证）
2. YAML 重写（W1）                              → commit 2
3. 治理门脚本 + selftest（W3）                   → commit 3
4. CI 接线 + oasdiff workflow + 豁免清单（W4/W5） → commit 4
5. baseline 再生（W6）+ 文档同步（§八）           → commit 5
6. 验收证据（W7 脚本与输出记录）                  → PR 描述附证据
```

分支：`feat/openapi-spec-governance-m0`。每 commit 独立可回滚；commit 2 依赖 commit 1（否则门尚未存在，无交叉约束）。

## 十、风险与回退

| 风险 | 缓解 |
|------|------|
| 指纹测试基线更新后其它测试依赖旧集合 | 全量后端测试跑通再提交（full-backend-test 8 步） |
| oasdiff ignore 机制与预期不符（版本差异） | 实施首步先下载二进制本地实跑 3 个用例（无变更/删路径/改 schema），再写 workflow |
| openapi-python-client 对 form requestBody 生成质量不足 | 验收 W7 前置试跑；若 form 支持差，schema 写法调整为 x-www-form-urlencoded 的最简 object 形态（不牺牲契约准确性） |
| YAML 重写体积大、评审困难 | 只动该动的：P0 端点重写 + 端点对账增删；P1/P2 条目原样保留（diff 可审计） |
| 指纹 .cc 解析被未来重构破坏 | 解析兜底下限 + 测试文件头注释声明耦合关系 |

## 附录 A：P0 端点 wire 契约实测依据（2026-08-16）

> 由控制器源码 + 集成测试核查，file:line 略（存于任务过程记录）。关键结论：

- 全局无成功包络；错误双形态：`/oauth2/*`、`/.well-known/*` = RFC 6749 体；其余 = `{"error": {...}}` 包络（含 numeric_code/details）
- token 成功响应**不含** scope 字段（authorization_code grant），含 roles；client_credentials/device 才回 scope；id_token 仅 openid scope 且 JWK 就绪时出现
- introspect inactive = `{"active": false}`；无 username 字段
- revoke 成功 = 200 空 body
- userinfo 401/403 为 RFC 体（invalid_token / insufficient_scope + WWW-Authenticate）
- end_session 400 为纯文本 body（非 JSON）——spec 中以 description 注明
- device_authorization 响应无 verification_uri_complete；interval=5, expires_in=600
- login 支持 JSON body（`json=true` 时 200 JSON `{code, location}`，否则 302）
- `/api/me/mfa/setup` 200 = `{secret, otpauth_uri, message}`；`verify` 200 = `{message, backup_codes[10]}`；均 Bearer 保护
- health 三端点：live 恒 200 `{status: ok}`；ready 200/503 依 DB/Redis
