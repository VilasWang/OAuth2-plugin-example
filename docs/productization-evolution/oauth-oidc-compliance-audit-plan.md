# OAuth/OIDC 规范性审查计划 — authforge

> 本文件是审查**计划**（检查要点、检查方法、判定标准），是评估执行前的设计文档。
> 评估结果（符合性评级、发现、整改）见配套报告 [`oauth-oidc-compliance-audit.md`](oauth-oidc-compliance-audit.md)。
>
> | 项目 | 内容 |
> |---|---|
> | 计划日期 | 2026-08-07 |
> | 代码基线 | `test/coverage-push` @ 09ebf8d |
> | 计划状态 | 已通过评审，已执行（见报告） |

---

## 一、审查范围与方法论

### 1.1 审查目标
基于客观代码事实，逐条对照 OAuth/OIDC 核心规范评估 authforge 实现的符合性，输出结构化报告。**所有判断必须引用规范章节 + 代码 `file:line`，禁止主观推测**。

### 1.2 审查对象（代码地图）

| 层级 | 关键文件 | 对应规范 |
|---|---|---|
| 授权端点 | `libs/drogon/src/controllers/AuthorizationEndpointController.cc`、`SessionController.cc`（login/consent） | RFC 6749 §3.1/§4.1、RFC 7636、OIDC §3.1.2 |
| 令牌端点 | `libs/drogon/src/controllers/TokenEndpointController.cc`、`libs/oauth2/src/protocol/TokenService.cc` | RFC 6749 §3.2/§4.1.3/§4.4/§6、RFC 8628 |
| 客户端认证 | `TokenEndpointController.cc`（Basic/Post 提取）、各 `*ClientRepository::validateClient` | RFC 6749 §2.3.1、§3.2.1 |
| PKCE | `libs/oauth2/src/pkce/Pkce.cc`、`TokenService.cc` | RFC 7636 §4 |
| Introspect/Revoke | `TokenEndpointController.cc`、`OAuth2ErrorHandler.cc` | RFC 7662、RFC 7009 |
| OIDC Discovery/JWKS | `DiscoveryController.cc`、`JwkManager.cc` | RFC 8414、OIDC Discovery 1.0、RFC 7517 |
| UserInfo | `TokenEndpointController.cc`、`OAuth2AuthFilter.cc` | OIDC Core §5.3、RFC 6750 |
| 设备授权 | `DeviceAuthController.cc`、`TokenEndpointController.cc`(device_code 分支) | RFC 8628 |
| 注销 | `SessionController.cc::logout` | OIDC RP-Initiated Logout、Session Mgmt |
| 客户端注册 | `ClientRegistrationController.cc`、`ClientRegistrationService.cc` | RFC 7591、RFC 7592 |
| 存储/哈希 | `libs/oauth2/src/protocol/TokenCrypto.cc`、`PostgresTokenRepository.cc`、`PostgresClientRepository.cc`、`RedisTokenRepository.cc`、`MemoryClientRepository.cc` | RFC 6749 §10.4/§10.6、安全 BCP |

### 1.3 评估尺度（评级标准）

| 评级 | 定义 |
|---|---|
| **符合** | 该规范条款的代码实现满足其 MUST/SHOULD 要求，无功能或安全偏差 |
| **部分符合** | 实现了核心要求，但存在偏差：缺字段、缺边角 MUST、或不满足 SHOULD（需说明影响） |
| **不符合** | 违反某条 MUST，或实现存在功能性/安全性错误 |
| **未实现** | 规范定义了能力但代码中无对应实现（含 advertised-but-missing、declared-but-stub） |

### 1.4 风险等级标准

| 等级 | 标准 |
|---|---|
| **严重 (Critical)** | 可被利用的认证/授权绕过、令牌泄露、注入或崩溃 |
| **高 (High)** | 违反 MUST、特定部署下产生实质安全/互操作缺陷 |
| **中 (Medium)** | 违反 SHOULD、缺少推荐的加固，默认配置下可接受 |
| **低 (Low)** | 一致性/文档/边角问题，影响有限 |

### 1.5 证据采集方法
- **代码静态核验**：`file:line` + 短摘录，必要时 `codegraph explore` / Grep 复核调用链
- **调用路径追踪**：跨 Controller→Plugin→Service→Repository 的异步链
- **规范条文引用**：每个判断标注 RFC 章节 + 关键 MUST/SHOULD 原文（中文译述）
- **多后端差异**：Postgres / Redis / Memory 三套实现分别评估（已知存在差异）

### 1.6 OIDC profile 分档（按用户确认）
- **核心 MUST 档**：OIDC Core 中标 MUST 的条款（如 id_token 必备 claim、UserInfo 须校验 openid scope）
- **扩展 SHOULD 档**：OIDC Core 的 SHOULD 条款，以及 Session Management / RP-Initiated Logout / Back-Channel Logout 等独立规范（这些虽非 OIDC Core 的 MUST，但是「可互操作的 OIDC Provider」的事实标配）

---

## 二、规范清单与逐项检查要点

> 以下每条规范给出：**检查条款（章节）** → **检查方法** → **符合性判定标准**。条款不限于项目已声明范围，覆盖全部核心规范。

### 规范 1 — RFC 6749 OAuth 2.0 Authorization Framework

| 章节 | 检查要点 | 检查方法 | 判定标准 |
|---|---|---|---|
| §1.6 / §3.1.1 | 协议端点须运行于 HTTPS；授权码重定向须 https（loopback 例外） | Grep scheme 校验、`redirect_uri` 注册/匹配逻辑 | 重定向 URI 是否做 scheme 强制？是否有 loopback (`http://127.0.0.1/localhost`) 例外 |
| §3.1.2.1 | redirect_uri 注册要求、loopback 例外、动态构造查询部分 | `Client.h::isRegisteredRedirectUri`、`RuleEngine` | 是否精确匹配；loopback 端口通配是否实现 |
| §3.1.2.3 | redirect_uri 须**精确匹配**，禁止通配/前缀 | `Client.h:86-90`、`ClientService.cc:32-44` | 必须精确匹配 |
| §4.1.1 | 授权请求参数解析（response_type/state/scope/redirect_uri/code_challenge/nonce） | `AuthorizationEndpointController.cc` 100-200 | 各参数读取/校验完备性 |
| §4.1.2 | 授权码 TTL ≤10 分钟、一次性、绑定 client/redirect/scope | `TokenService.cc` 授权码生成/`consumeAuthCode` | TTL=600s、单次、绑定字段 |
| §4.1.2.1 | **错误重定向 vs 直接错误**的分流规则 | `AuthorizationEndpointController.cc` 各错误分支 | redirect_uri 有效时必须 `?error=&state=` 重定向；仅当 redirect_uri 无效/客户端无效才直接 4xx |
| §4.1.3 | code 兑换：redirect_uri 须与签发时一致；client 绑定校验 | `TokenService.cc:177-231`、`*GrantRepository::consumeAuthCode` | exchange 时 redirect_uri 是否参与比较；空 redirect_uri 是否绕过比较 |
| §4.1.3.1 | 安全考量：授权码重用须级联吊销已发令牌 | code 重放路径 | 是否存在级联吊销（已知：refresh 有，code 无） |
| §4.4 | client_credentials：仅限 confidential 客户端、不发 refresh_token、scope 校验 | `TokenEndpointController.cc:712-867` | client_type 校验、refresh_token 缺省、scope 子集校验 |
| §3.2.1 / §4.1.3 | token 端点对所有 confidential 客户端**必须**认证 | 4 种 grant 的 `validateClient` 调用点 | **重点核**：refresh_token 路径是否漏认证 |
| §5.1 | 成功响应：token_type=Bearer、expires_in、scope 回显、refresh_token 出现规则、Cache-Control:no-store | `TokenService.cc`/`TokenEndpointController.cc` 各成功响应构造 | 字段完整性 + no-store 头 |
| §5.2 | 错误响应：error 码集合、HTTP 状态（400/401+WWW-Authenticate/500/503） | `OAuth2ErrorHandler.cc`、`ErrorCatalog.cc`、`HttpResponder.cc` | error 码与状态码映射；invalid_client 是否带 WWW-Authenticate；validation gate 是否误用应用信封 |
| §6 | refresh_token：轮换、重用检测、绑定 client_id | `TokenService.cc:338-441`、`atomicRevokeRefreshToken`、`revokeTokenFamily` | 轮换 + 级联 + reuse-detection |
| §10.4 / §10.6 | 凭证保护：access/refresh token 与 client_secret 须安全存储 | `TokenCrypto.cc::hashToken`、各 ClientRepository | 哈希算法一致性、常量时间比较、是否明文 |
| §10.9 | 须维护已撤销令牌集以拒绝被撤销令牌 | `validateAccessToken`、revoked 标志 | 撤销后是否立即失效 |

### 规范 2 — RFC 6750 Bearer Token Usage

| 章节 | 检查要点 | 检查方法 | 判定标准 |
|---|---|---|---|
| §2.1 | Authorization: Bearer 解析 | `OAuth2AuthFilter.cc:39-52`、`AuthorizationFilter.cc` | 大小写、scheme 校验 |
| §2.3 | query/body 传 token（已废弃）| Grep `access_token` 查询参数使用 | 是否允许（不推荐） |
| §3 | WWW-Authenticate: Bearer realm/error/error_description 在 401/403 上的构造 | 资源端点 401 响应构造 | 是否发出 RFC 6750 §3 challenge |
| §3.1 | insufficient_scope 错误码 | scope 强制逻辑 | 是否有端点按 scope 拒绝（已知疑点：filter 存 scope 不校验） |

### 规范 3 — RFC 7662 Token Introspection

| 章节 | 检查要点 | 检查方法 | 判定标准 |
|---|---|---|---|
| §2.1 | **必须**要求客户端认证 | `TokenEndpointController.cc:279-345` | 是否做 Basic/Post 认证 |
| §2.2 | 响应字段：active、scope、client_id、username、token_type、exp、iat、nbf、sub、aud、iss、jti | introspect 响应构造 :377-409 | 缺哪些（已知缺 jti/username） |
| §2.2 | 无效/过期/撤销令牌须返回 active=false（非 4xx） | :353-368、repo 层 active=false 分支 | 不返回错误，返回 active=false |
| §2.3 | token_type_hint 可忽略但应接受 | `RuleSet.cc:573` | 参数被接受 |

### 规范 4 — RFC 7009 Token Revocation

| 章节 | 检查要点 | 检查方法 | 判定标准 |
|---|---|---|---|
| §2.1 | 客户端认证；客户端只能撤销自己的令牌 | `TokenEndpointController.cc:420-523` | 认证 + 所有权校验 |
| §2.1 | 支持 access 与 refresh 两类 token | revokeAccessToken 与 refresh 表回退 | 是否两类都支持 |
| §2.2.1 | 成功/未知令牌均返回 200；Cache-Control:no-store | :494-544、`createSuccessResponse` | 始终 200；no-store 头 |

### 规范 5 — RFC 7636 PKCE

| 章节 | 检查要点 | 检查方法 | 判定标准 |
|---|---|---|---|
| §4.3 | code_challenge_method 仅 plain/S256；不支持 S256 应拒绝 | `AuthorizationEndpointController.cc` method 校验、`Pkce.cc` | authorize 时是否校验方法集合 |
| §4.4/§4.6 | S256 须 BASE64URL(SHA256(raw))；plain 直传 | `Pkce.cc::computeCodeChallenge` | 是否正确实现（已知：libs/oauth2 实现正确） |
| §4.6 | code_verifier 重算比较 | `TokenService.cc:206-219` | exchange 时校验 |
| BCP §2.1.1 / RFC 9700 | **建议**所有 code-flow 客户端强制 PKCE | `auth.require_pkce_for_public` 默认值 | 默认是否强制（已知：默认 off） |

### 规范 6 — RFC 8252 Native Apps（适用性评估）

| 章节 | 检查要点 | 检查方法 | 判定标准 |
|---|---|---|---|
| §7.3 | loopback redirect 端口通配、授权码+PKCE 推荐给 native app | redirect_uri 处理 + PKCE 强制 | 是否支持 native app 场景（如未支持则"未实现"，按适用性评级） |
| §8.1 | public client 必须用 PKCE | 公共客户端 PKCE 强制点 | 是否强制 |

### 规范 7 — RFC 8628 Device Authorization Grant

| 章节 | 检查要点 | 检查方法 | 判定标准 |
|---|---|---|---|
| §3.1.1/§3.4 | device_authorization 与 token 端点对 confidential 客户端的认证 | `DeviceAuthController.cc:156`、`TokenEndpointController.cc:1227-1276` | device_authorization 是否认证（已知疑点：空 secret 调用） |
| §3.2 | 响应字段：device_code/user_code/verification_uri/[verification_uri_complete]/expires_in/interval | `DeviceAuthController.cc:202-213` | verification_uri_complete 缺失（可选）；user_code 字符集 |
| §3.5 | polling 错误码：authorization_pending、slow_down、expired_token、access_denied + HTTP 400 | `TokenEndpointController.cc:921-1225` | **重点**：slow_down 是否发出（已知：定义但未发出） |
| §5.2 | user_code 字符集剔除歧义字符 | `DeviceAuthController.cc:75` | 字符集 |

### 规范 8 — RFC 8414 OAuth 2.0 Authorization Server Metadata + OIDC Discovery 1.0

| 章节 | 检查要点 | 检查方法 | 判定标准 |
|---|---|---|---|
| OIDC Discovery §4 | issuer/authorization_endpoint/token_endpoint/userinfo_endpoint/jwks_uri/end_session_endpoint/registration_endpoint/introspection/revocation_endpoint | `DiscoveryController.cc::oidcDiscovery` | 缺 end_session_endpoint、registration_endpoint |
| OIDC Discovery §3 | issuer 须与签发的 iss claim 精确一致（含 https、无尾斜杠） | `baseUrl` 与 token iss 来源对比 | **重点**：refresh introspect iss 硬编码 `https://oauth.example.com` |
| 字段完备性 | response_types/grant_types/subject_types/id_token_signing_alg/scopes/token_endpoint_auth_methods/claims_supported/code_challenge_methods_supported | 同上 | 列表与实际能力是否一致 |
| RFC 8414 §2 | oauth-authorization-server 文档字段 | `metadata()` | 字段齐全 |

### 规范 9 — OIDC Core 1.0

| 章节 | 检查要点 | 检查方法 | 判定标准 |
|---|---|---|---|
| §2 | id_token claims：iss/sub/aud/exp/iat/nonce/auth_time/acr/amr/azp | `TokenService.cc:301-324` 签发逻辑 | **重点**：缺 auth_time/acr/amr/azp |
| §3.1.2.1 | 请求参数：prompt（none/login/consent/select_account）、max_age、nonce | Grep prompt/max_age | **已知未实现** |
| §3.1.3.7 | auth_time/max_age/acr 校验、nonce 校验 | 同上 | 未实现 |
| §3.1.3.6 | id_token 仅对 openid scope 签发 | `TokenService.cc` scope 判断 | 已实现 |
| §5.3 | UserInfo 须校验 openid scope、须支持 CORS、返回 sub | `TokenEndpointController.cc:1297-1389` | **重点**：不校验 scope、无 CORS |
| §7.2.1 | UserInfo 端点 CORS 强制 | OPTIONS/响应头 | 未实现 |
| §12 | refresh 时若原 openid 须重发 id_token | `refreshAccessToken` | **已知缺失** |
| §10.1 | nonce 单次使用防重放 | nonce 存储/校验 | 仅 echo 不校验唯一性 |
| §15 | pairwise/stable sub 标识符 | sub 来源 | 用内部 userId（非 pairwise） |
| OIDC RP-Initiated Logout | end_session_endpoint、id_token_hint、post_logout_redirect_uri、state | Grep end_session | **已知未实现** |
| OIDC Back-Channel Logout | backchannel_logout_uri、logout token | stub `sendBackchannelLogoutNotifications` | 已知为 stub |

### 规范 10 — RFC 7519 / 7517 / 7515（JWT/JWK/JWS，支撑核验）

| 章节 | 检查要点 | 检查方法 | 判定标准 |
|---|---|---|---|
| JWT §4.1 标准 claim | iss/sub/aud/exp/iat/jti/nonce | id_token 签发 | 缺 jti |
| JWK §4 | kty/use/alg/kid/n/e | `JwkManager::getJwks` | 字段正确 |
| 私钥保护 | JWKS 仅暴露公钥 n/e | `getPublicKeyComponents` | 不暴露 d/p/q |
| kid 选择/轮转 | 每个 token 的 kid 选择逻辑 | `JwkManager` 单 kid、init-once | **无轮转** |
| alg=none 防护 | 签名算法白名单 | `signJwt` 头部 | 仅 RS256 |

### 规范 11 — RFC 7591 / 7592 动态客户端注册（适用性）

| 章节 | 检查要点 | 检查方法 | 判定标准 |
|---|---|---|---|
| RFC 7591 §3 | 动态注册请求/响应形态 | `ClientRegistrationService.cc` | 形态正确 |
| RFC 7591 §2.0 | 是否开放注册 vs 管理员授权 | AuthorizationFilter 前置 | gated by admin（偏离纯开放模型） |
| RFC 7592 | registration_access_token、CRUD by client | Grep register/{id}、registration_access_token | **未实现** |
| token_endpoint_auth_method 持久化 | 注册接受但未入库 | `Oauth2Clients` 列、注册写入点 | **已知未持久化** |

### 规范 12 — RFC 9068 JWT-format Access Tokens（适用性）

| 章节 | 检查要点 | 检查方法 | 判定标准 |
|---|---|---|---|
| §2.2 | access token 若为 JWT 的 claim 形态 | access token 生成 | access token 为 opaque（不适用，评级"未实现/不适用"并说明） |

### 规范 13 — RFC 9700 / OAuth 2.0 Security BCP（横向加固）

| 章节 | 检查要点 | 检查方法 | 判定标准 |
|---|---|---|---|
| §2.1.1 | 强制 PKCE | 同 PKCE §4.6 检查 | 默认 off |
| §2.2.1 | redirect_uri 精确匹配 | 同 RFC 6749 §3.1.2.3 | 已满足 |
| §2.4 | token 端点限流/防爆破 | Grep rate limit | 未实现 |
| §4.9 | 安全存储凭证、防御时序攻击 | secret 比较实现 | Redis 非常量时间 |
| §4.10 | id_token/auth_time 校验 | 同 OIDC | 未实现 |

### 规范 14 — 横向安全与运维（跨规范聚合）

| 主题 | 检查要点 | 检查方法 |
|---|---|---|
| Client-secret 哈希一致性 | 写入路径 vs 校验路径算法 | `ClientRegistrationService.cc:143` vs `PostgresClientRepository.cc:231`、`RedisClientRepository.cc:165` |
| 令牌哈希一致性 | UPPER-hex SHA-256 | `TokenCrypto.cc::hashToken` |
| 明文密钥存储 | Memory 后端明文 | `MemoryClientRepository.cc:67` |
| 限流 | 4 端点无节流 | Grep |
| 日志脱敏 | raw token 是否进日志 | 日志点审计 |
| CORS | 仅 doc 端点 | Grep Access-Control |
| open redirect | redirect_uri 拼接 | `AuthorizationEndpointController.cc:468`（state 未 urlEncode） |

---

## 三、执行计划与交付物

### 3.1 执行步骤（实施阶段，评审通过后启动）

1. **逐规范核验**：按上述 14 个规范、~90 条检查项，每项采集代码证据 + 规范引文，记录到工作笔记。
2. **多后端分别评级**：Postgres / Redis / Memory 三套存储在同一条款下若行为不同，分别给评级（重点：client-secret 哈希、常量时间比较、refresh 存储可用性）。
3. **交叉验证可疑条目**：对每个"不符合/部分符合"，二次 Grep/Read 复核调用链，避免误判。
4. **汇总风险矩阵**：按风险等级排序所有发现。

### 3.2 最终报告结构（交付物）

1. **执行摘要**：总体符合度概览、风险等级分布、最高优先级 3-5 项。
2. **规范逐章评估**：每规范一节 → 符合性总评 + 逐条款表（条款 / 评级 / 证据 / 偏差描述）。
3. **发现清单**：编号化 F-001…，每条含：规范引用、现象、根因 `file:line`、风险等级、影响、整改建议。
4. **分阶段整改计划**：P0（严重/高，认证与令牌安全）/ P1（中，OIDC 完备性、加固）/ P2（低，一致性、文档），每阶段含工作项、估计改动范围、验收标准。
5. **附录**：代码地图、评级尺度、后端差异矩阵、未核验项声明。

### 3.3 声明的边界
- 本审查为**静态代码审查 + 规范符合性评估**，不含动态渗透测试、不含性能/可用性评估。
- 会明确标注"未核验"项（如未跑过的运行时行为、未审的测试代码）。
- 报告中所有"已知/疑点"均在实施阶段用代码二次复核后定稿，不复用未经验证的判断。

---

**计划版本**：v1.0 | **计划日期**：2026-08-07 | **评审状态**：已通过 | **执行结果**：见 [`oauth-oidc-compliance-audit.md`](oauth-oidc-compliance-audit.md)
