# B2 社交账号 link/unlink — 技术方案

> **创建日期**: 2026-08-21
> **上游计划**: [next-phase-implementation-plan.md](../next-phase-implementation-plan.md) §三 B2
> **IAM 审计依据**: [iam-architecture-audit.md](../iam-architecture-audit.md) §二/§四（P1 缺口：「账号关联 link/unlink — 未实现，用户无法在自助门户手动关联/解绑社交账号」）
> **状态**: 已实现（分支 `feat/social-link-unlink`，随实现评审更新：`listForUser` 排除 `provider='local'` 种子映射行——种子/密码流会给每个用户种一行 local 映射，它不是社交身份，不得出现在列表、也不计入守卫；POST 200 响应不含 `linked_at`（插入回调不回读行，前端 link 后重新拉列表））

---

## 一、背景与目标

AuthForge 已有三家社交登录提供商（Google/GitHub/WeChat，条件编译 `WITH_SOCIAL`），其中
GitHub 是唯一做「find-or-create 本地账号 + subject mapping」的 provider；用户无法在自助
门户查看、主动关联（link）或解除（unlink）社交账号。本任务（B2，IAM P1 缺口）补齐这一能力：

1. 已登录用户可以把自己的 GitHub/Google/WeChat 身份关联到当前本地账号；
2. 可以解除关联；
3. 可以列出当前已关联的社交身份；
4. 解除关联不得把用户锁死在系统外（最后凭证守卫）。

**工程量口径**（与上游计划一致）: 3–5 天。

## 二、现状分析（实现前逐项核实过源码）

| 组件 | 现状 | 对本设计的影响 |
|---|---|---|
| `oauth2_subject_mappings` 表 | `UNIQUE(provider, subject)`；`internal_user_id → users(id) ON DELETE CASCADE`；无 `(provider, internal_user_id)` 唯一约束 | 一个社交身份全局只能映射到一个本地账号（DB 强制）；「一个用户每 provider 一条映射」需服务层保证 |
| `ISocialAccountRepository` | 两个方法：`findLinkedUser(provider, subject)`（带 V024 软删除/锁定契约）、`createLinkedUser(...)`；仅 GitHubAuthService 消费 | link/unlink 是同一有界关注点（社交身份↔本地账号生命周期），**扩展现有接口**而非新建 |
| `GoogleAuthService::login` | code → profile（含 `sub`）即返回，不落库 | link 流程可直接复用（subject = `profile.sub`） |
| `WeChatAuthService::login` | code → profile（含 `openid`）即返回，不落库 | 同上（subject = `profile.openid`） |
| `GitHubAuthService::login` | code → **find-or-create 本地账号** → 返回本地 userId | 对 link 无用：link 需要 profile-only 路径（github 数字 id），需新增 `fetchProfile` |
| `UserSelfServiceController` | `/api/me`、`/api/me/password`、`/api/me/authorized-apps`(+DELETE)、`/api/me`(DELETE)；全部 `OAuth2AuthFilter` + scope `profile`；`#54` 一律 `deleted_at IS NULL` 过滤；`[this]` 控制器豁免 | 新端点挂这里，复用 `selfServiceEp`（scope registry）与 public_sub→internal id 解析模式 |
| 前端 user 门户 | `LoginPage` 的 GitHub 按钮直接跳 `github.com/login/oauth/authorize`，`GitHubCallbackPage` 拿 code POST `/api/github/login`；`SecurityPage` 已有密码/MFA/WebAuthn 卡片 | link 流程复用同一 SPA-拿-code-后-POST 模式；「已关联账号」卡片放 SecurityPage |
| 密码哈希 | `PasswordHasher` 输出 `$pbkdf2-sha256$...` 前缀；社交建号写入的是 64 位随机 hex（`createLinkedUser`）；`deleteAccount` 写 `"DELETED"`；`generateSecureToken()` 是 Base64URL（不可能以 `$` 开头） | 「用户是否有可用密码」可以用 `password_hash LIKE '$pbkdf2-sha256$%'` 判定 → 最后凭证守卫可实现 |
| **社交 token 双键**（#54/#56 已知缺口） | GitHub 登录签发的 token `user_id` 存**内部数字 id**（`issueTokensForUser`），而 `/api/me*` 按 `public_sub` 解析（`UserSelfServiceController.cc:104`）——纯社交会话调 `/api/me` 会 404。userinfo 已有 dispatch 先例（`OAuth2Plugin.cc` 「numeric → findById；否则 → findByPublicSub」） | 本特性的主要画像恰是社交建号用户；新 handler 的用户解析**镜像 userinfo 的 numeric dispatch**（见 §4.5），社交会话可用 |
| 测试基建 | `FakeOAuthHttpClient` / `FakeSocialAccountRepository` / `SocialMockFixture`（进程级 static 注入 + `DrClassMap` 单例 seam）均已存在 | 单测与 HTTP 集成测试有现成模式可循 |

## 三、API 设计

三个新端点，全部挂在 `UserSelfServiceController`，`OAuth2AuthFilter` 保护，scope 要求
`profile`（与现有 `/api/me*` 一致，无 `impliedBy`——自服务必须是真实用户 token）。

### 3.1 `GET /api/me/social/links` — 列出已关联社交身份

**成功** `200`:
```json
{
  "social_links": [
    { "provider": "github", "subject": "12345678", "linked_at": "2026-08-21T12:00:00.000000Z" }
  ],
  "total": 1
}
```
- `provider`: `github|google|wechat`；`subject`: provider 侧稳定 id（GitHub 数字 id / Google `sub` / WeChat `openid`）。
- 用户不存在/软删除（token 有效但用户行没了）→ `404 VALIDATION_RESOURCE_NOT_FOUND`（与 getProfile 一致）。
- 服务未装配（`WITH_SOCIAL=ON` 但 IdentityAssembly 未注入，如 memory 模式裸跑）→ `500 INTERNAL_ERROR`（见 §3.4；`WITH_SOCIAL=OFF` 时路由不存在，drogon 层 404）。

### 3.2 `POST /api/me/social/links/{provider}` — 关联（完成 OAuth 流）

请求体（JSON，与 `/api/github/login` 同形状）:
```json
{ "code": "provider_authorization_code" }
```

**语义**（「前端跳转拿 code、后端验 code」两段式，前端负责跳 provider，后端负责 code→token→profile 的机密交换）:
1. 服务端用配置的 `external_auth.{provider}` 凭证交换 code、拉取 profile，得到 provider subject；
2. 冲突检查：
   - `(provider, subject)` 已映射到**别的用户** → `409 VALIDATION_RESOURCE_CONFLICT`（message 不泄露归属方）；
   - `(provider, subject)` 已映射到**当前用户** → `409`（幂等语义明确化：UI 从 GET 列表已知状态，重复提交即冲突，提示 already linked）；
   - 当前用户在该 provider 已有**另一条**映射（换了 GitHub 账号）→ `409`，提示先 unlink；
3. 通过检查 → INSERT 映射行 → `200`:
```json
{ "provider": "github", "subject": "12345678", "message": "Social account linked successfully" }
```
（`linked_at` 仅由 GET 列表返回；插入回调不回读行。）

竞态：两请求同时通过预检时，DB `UNIQUE(provider, subject)` 兜底，插入冲突映射为 `409`（AlreadyLinkedToOtherUser）。

**竞态窗口注记**（PR 评审）：UNIQUE 只覆盖「同 subject」竞争；**同一用户并发 link 同一
provider 的两个不同 subject**（两个 tab 各拿一个 GitHub 账号的 code）两路预检都会通过、
两路插入都不触发 UNIQUE——结果是该用户在该 provider 下有两条映射。后果有限（两条都属
于本人；list 都会展示；unlink 按 provider 一次清光；登录解析取其一），属设计 D5 已接受
的服务层不变式缺口，非安全洞。

### 3.3 `DELETE /api/me/social/links/{provider}` — 解除关联

**语义**:
1. 当前用户在该 provider 无映射 → `404 VALIDATION_RESOURCE_NOT_FOUND`；
2. **最后凭证守卫**：这是用户最后一条社交映射，且用户**没有可用密码**（`password_hash` 不匹配 `$pbkdf2-sha256$` 前缀 —— 社交建号的随机 hex 与 `"DELETED"` 都不算）→ `409 VALIDATION_RESOURCE_CONFLICT`，message 说明会失去唯一登录方式；
3. 通过 → DELETE 映射行 → `200`:
```json
{ "provider": "github", "subject": "12345678", "message": "Social account unlinked successfully" }
```

不吊销现有 token（解绑社交身份不影响已发的会话；与 Keycloak 行为一致；已在 openapi
DELETE 描述中向调用方披露——PR 评审 W3）。

**解绑后该 provider 身份的重新登录行为**（PR 评审 W4）：映射删除后本地 `gh_<login>` 用户
行仍在；该 GitHub 身份再次登录会走 find-or-create 的 create 分支，`ON CONFLICT (username)
DO NOTHING` 因用户名已被（自己旧行）占用而失败，登录收到 `DB_QUERY_ERROR`——**在重新
link 之前不会自愈**。UI 的解绑确认弹窗已提示"重新 link 前无法用它登录"；把该场景改为
可操作的错误提示（区分用户名冲突与真实 DB 故障）归入 #70（登录对齐）一并处理。

**已知竞态（自查 + PR 评审 #3）**：守卫是 check-then-act——两个并发 unlink 各自观察到
`size==2` 而同时放行，可能把无密码用户删到零凭证（自我造成的锁定，需管理员恢复）。与 link
侧不同（DB UNIQUE 约束兜底），此处无廉价兜底（后置复查或按用户串行化成本不成比例）；记为
已知限制，风险接受理由：需要用户主动对自己的账号并发发起解绑。

### 3.4 错误映射（全部走 ErrorResponder Error Envelope）

| 场景 | HTTP | Error_Code |
|---|---|---|
| 无/坏 token | 401 | （filter 层） |
| provider 不是 `github\|google\|wechat` | 400 | `VALIDATION_INVALID_INPUT` |
| 缺 `code`（POST） | 400 | `VALIDATION_MISSING_REQUIRED_FIELD` |
| provider 凭证未配置 / 服务未装配 | 500 | `INTERNAL_ERROR` |
| code 交换网络失败 | 502 | `NET_CONNECTION_FAILED` |
| code 无效/provider 拒绝 | 400 | `VALIDATION_INVALID_INPUT` |
| 已关联（自己或他人） | 409 | `VALIDATION_RESOURCE_CONFLICT` |
| 无该映射（DELETE） | 404 | `VALIDATION_RESOURCE_NOT_FOUND` |
| 最后凭证守卫 | 409 | `VALIDATION_RESOURCE_CONFLICT` |
| 用户不存在/软删除 | 404 | `VALIDATION_RESOURCE_NOT_FOUND` |
| DB 失败 | 500 | `DB_QUERY_ERROR` |

不新增 Error_Code（全部复用已注册条目）；不泄露「某社交身份属于谁」（冲突 message 固定措辞）。

### 3.5 审计事件

`DrogonAuditSink::logFromRequest`，与 `password_changed`/`app_authorization_revoked` 同模式：
- `social_account_linked`（success）/ `social_account_link_failed`（failure，409/502 等）
- `social_account_unlinked`（success）/ `social_account_unlink_blocked`（守卫触发）

## 四、架构设计

### 4.1 分层与依赖方向

```
UserSelfServiceController (libs/drogon, WITH_SOCIAL)
    │  public_sub → internal id（Mapper<Users>，#54 软删除过滤）
    │  错误映射 + 审计 + 注入 seam（setSocialLinkService）
    ▼
SocialLinkService (libs/identity, WITH_SOCIAL, 新建)
    │  编排：验证 code → subject → 冲突预检 → 守卫 → 写映射
    ├── GoogleAuthService / WeChatAuthService  （既有 login()，profile-only）
    ├── GitHubAuthService::fetchProfile        （新增，仅交换+取 profile）
    ▼
ISocialAccountRepository (libs/identity, 扩展 4 个方法)
    ├── PostgresSocialAccountRepository (Mapper 实现)
    └── FakeSocialAccountRepository (testing)
```

依赖方向与现有 identity 层一致（service 不碰 Drogon/DB 类型，HTTP 走 `IOAuthHttpClient`
端口，持久化走 repo 端口）。控制器层 `[this]` 豁免；identity service 按仓库规则捕获
`shared_from_this()`/按值拷贝依赖，不用 `[this]`（`SocialLinkService` 无状态，依赖按值
capture 进 lambda，与 `GoogleAuthService` 等先例一致）。

### 4.2 `SocialLinkService` 接口（新）

```cpp
// libs/identity/include/authforge/identity/SocialLinkService.h  (#ifdef WITH_SOCIAL)

enum class SocialLinkOpStatus {
    Ok,                     ///< 成功（link 或 unlink）
    InvalidProvider,        ///< provider 不在 {github, google, wechat}
    ExchangeFailed,         ///< code 交换/userinfo 失败（errorCode 携带细化码）
    AlreadyLinkedToSelf,    ///< (provider,subject) 已映射到当前用户
    AlreadyLinkedToOtherUser, ///< (provider,subject) 已映射到其他用户（不泄露谁）
    ProviderConflictForUser,  ///< 当前用户在该 provider 已有另一条映射
    NoLink,                 ///< unlink: 当前用户在该 provider 无映射
    LastCredentialGuard,    ///< unlink: 最后一条社交映射且无可用密码
    RepositoryError,
};

struct SocialLinkEntry { std::string provider; std::string subject; std::string linkedAt; };
struct SocialLinkOpResult { SocialLinkOpStatus status; std::string errorCode; SocialLinkEntry entry; };

class SocialLinkService {
  public:
    SocialLinkService(
      std::shared_ptr<GitHubAuthService> github,
      std::shared_ptr<GoogleAuthService> google,
      std::shared_ptr<WeChatAuthService> wechat,
      std::shared_ptr<ISocialAccountRepository> accountRepo);

    void linkAccount(const std::string &provider, const std::string &code,
                     int32_t internalUserId, std::function<void(SocialLinkOpResult)> &&cb);
    void unlinkAccount(const std::string &provider, int32_t internalUserId,
                       std::function<void(SocialLinkOpResult)> &&cb);
    void listAccounts(int32_t internalUserId,
                      std::function<void(SocialLinkOpStatus, std::vector<SocialLinkEntry>)> &&cb);
};
```

**link 编排**:
```
provider ∈ {github,google,wechat}? ── 否 → InvalidProvider
  │是
  ├─ github → GitHubAuthService::fetchProfile(code)  ─┐
  ├─ google → GoogleAuthService::login(code)          ├─ 失败 → ExchangeFailed(errorCode)
  └─ wechat → WeChatAuthService::login(code)         ─┘
  │得到 subject
  ├─ findLinkedUser(provider, subject)
  │    Linked(userId==当前) → AlreadyLinkedToSelf
  │    Linked(userId≠当前) → AlreadyLinkedToOtherUser
  │    AccountUnavailable  → AlreadyLinkedToOtherUser（映射被死账号占着，同 409 措辞）
  │    RepositoryError     → RepositoryError
  │    NoMapping ↓
  ├─ listForUser(internalUserId) 中该 provider 已有条目 → ProviderConflictForUser
  └─ insertLink(provider, subject, internalUserId)
       Inserted → Ok(entry)   Conflict → AlreadyLinkedToOtherUser（DB 竞态兜底）
```

**unlink 编排**:
```
provider 合法? ── 否 → InvalidProvider
  ├─ listForUser(internalUserId)
  │    RepositoryError → RepositoryError
  │    该 provider 无条目 → NoLink
  │    有条目且 links.size()==1 且 !userHasUsablePassword → LastCredentialGuard
  └─ deleteLink(provider, internalUserId) → Ok / NoLink(竞态) / RepositoryError
```

### 4.3 `ISocialAccountRepository` 扩展（加法，两个实现同步）

```cpp
using LinkEntriesCallback = std::function<void(std::optional<std::vector<SocialLinkEntry>>)>;
using LinkMutationCallback = std::function<void(LinkMutationStatus)>;  // Inserted/Conflict/Error
using BoolCallback = std::function<void(std::optional<bool>)>;         // nullopt = DB 错误

virtual void listForUser(int32_t internalUserId, LinkEntriesCallback &&cb) = 0;
virtual void insertLink(const std::string &provider, const std::string &subject,
                        int32_t internalUserId, LinkMutationCallback &&cb) = 0;
virtual void deleteLink(const std::string &provider, int32_t internalUserId,
                        LinkMutationCallback &&cb) = 0;   // Deleted/NoLink/Error 复用同一枚举
virtual void userHasUsablePassword(int32_t internalUserId, BoolCallback &&cb) = 0;
```

- `SocialLinkEntry` 上移到 `ISocialAccountRepository.h`（service 头复用）。
- Postgres 实现全部走 `Mapper<Oauth2SubjectMappings>`/`Mapper<Users>`：
  - `listForUser`: `findBy(_internal_user_id == id)`（顺带按 provider/subject 读回，无 JOIN）；
  - `insertLink`: `Mapper::insert`；错误回调以 `duplicate key` 子串判定唯一冲突（仓库既有先例
    `PostgresConsentRepository.cc:87`；libpq 的 `what()` 不含 SQLSTATE，23505 不可直接判）→ `Conflict`，
    其余 → `Error`；
  - `deleteLink`: `Mapper::deleteBy(provider==p && internal_user_id==id)`，affected 行数 0 → `NoLink`；
  - `userHasUsablePassword`: `Mapper<Users>.findBy(id==id && deleted_at IS NULL && password_hash Like '$pbkdf2-sha256$%')`，非空 → true。
  - 不新增任何 raw SQL（无 RETURNING 需求）。
- `FakeSocialAccountRepository` 同步实现（内存 vector）。

### 4.4 `GitHubAuthService::fetchProfile`（新增）

```cpp
struct GitHubProfileResult {
    std::string errorCode;   // 语义与 login() 的错误码一致
    int64_t githubId = 0;    // subject（字符串化后入库）
    std::string login;
    std::string email;
};
void fetchProfile(const std::string &code, std::function<void(GitHubProfileResult)> &&callback);
```

实现 = 现有 `login()` 的前两步（postForm token 交换 → getWithBearerToken `/user`）原样抽出，
`login()` 复用 `fetchProfile` 消除重复。profile-only、不触 repo——link 与 login 共用同一交换
逻辑，避免两份 provider 协议实现漂移。

### 4.5 控制器与装配

- `UserSelfServiceController.h`: `#ifdef WITH_SOCIAL` 包裹 3 条 `ADD_METHOD_TO`、3 个 handler
  声明、`setSocialLinkService(SocialLinkService *raw)`（进程单例 seam，`SocialMockFixture` 同款）；
  `WITH_SOCIAL` 关闭时路由根本不注册（CMake 排除的是社交控制器文件，本文件常驻，故用宏）。
- `initApiDocsImpl`: 3 条 `selfServiceEp`（自动带 `profile` scope 要求、`requiresAuth`）。
- handler 顺序（快速失败 + 前置校验不触库）:
  1. `socialLinkService_` 装配检查 → 500；
  2. provider 白名单校验 → 400；
  3. code 存在性校验（POST）→ 400；
  4. 用户解析（见下）→ 404；
  5. service 调用 → 状态映射（§3.4）+ 审计。

  **用户解析镜像 userinfo 的 numeric dispatch**（`OAuth2Plugin.cc` 先例）：token 的
  `userId` 属性**纯数字** → 按 `users.id` 查（GitHub 社交会话，token 存内部 id）；否则按
  `users.public_sub` 查（密码流会话）。两条路径都带 `deleted_at IS NULL`，空结果 → 404。
  不做 dispatch 的话，本特性最主要的画像（社交建号用户）反而用不了自己的 link/unlink（#54/#56
  双键缺口的直接后果）。
- `IdentityAssembly.cc`（`WITH_SOCIAL` 段）: 用已构造的三个 provider service + socialAccountRepo
  构造 `SocialLinkService`，`DrClassMap::getSingleInstance<UserSelfServiceController>()
  ->setSocialLinkService(...)`（进程级 static，与现有 wiring 同寿命语义）。

## 五、关键决策记录

| # | 决策 | 理由（含被否选项） |
|---|---|---|
| D1 | 补 `GET /api/me/social/links`（上游计划只列了 POST/DELETE） | 没有 list，前端无法渲染当前状态；三个端点是自助门户最小闭环 |
| D2 | 三家 provider 全支持 link/unlink（数据层）；**Google/WeChat 登录消费映射不做**（显式列为后续项） | Google/WeChat `login()` 已返回 subject，link 成本≈0；登录对齐是另一个改动面（两控制器发 token 逻辑），超出 3–5 天口径。GitHub link 立即作用于 GitHub 登录的映射查找；注意 GitHub 登录签发的社交会话受 #54/#56 双键缺口影响，本任务以 §4.5 的 numeric dispatch 保证该会话能用新端点（token 双键问题本身的根治不在本期） |
| D3 | link 流程 = 前端跳 provider 拿 code，`POST` 带 code 完成（非服务端发起+state 会话） | 与现有 `/api/github/login`、`GitHubCallbackPage` 模式一致；bearer token 已认证请求方，code 是单次短时效的。服务端 state 会话需要新存储+TTL，收益（防 login-CSRF 误关联）不抵复杂度；**遗留风险记入 §八** |
| D4 | 「已关联到自己」返回 409 而非幂等 200 | 显式冲突让 UI/调用方感知真实状态；幂等会掩盖「换了 GitHub 账号但旧映射还在」的 ProviderConflictForUser 情形 |
| D5 | 一个用户每 provider 至多一条映射（服务层约束，不加 DB 约束） | 换绑 GitHub 账号应显式 unlink→link；DB 加 `(provider, internal_user_id)` 唯一约束是迁移+orm-gen 连锁，收益低（服务层预检+本任务唯一入口已足够） |
| D6 | 最后凭证守卫 = 最后一条社交映射 && `password_hash` 不匹配 `$pbkdf2-sha256$` 前缀 | 防「社交建号用户解绑唯一凭证后永久锁死」。格式判定可靠：`PasswordHasher` 是唯一合法写入口（register/changePassword），社交建号写随机 hex、deleteAccount 写 `"DELETED"`。**WebAuthn 不计入守卫**（无密码+仅 WebAuthn 的用户解绑最后社交映射会被保守拦截——安全方向的假阳性，可接受，后续可扩展守卫输入）。邮箱找回不可作为放开理由（GitHub userinfo email 可能为 null，且找回依赖邮件基础设施可用性） |
| D7 | 扩展 `ISocialAccountRepository` 而非新建接口 | link/unlink 与 find-or-create 是同一有界关注点（该头文件自己的 scope 注释）；新接口会让「社交身份映射」散落两个端口 |
| D8 | 审计 4 事件（linked/unlinked + 2 failure） | 对齐 `password_change_failed`/`password_changed` 先例；IAM P1 缺口的本意包含合规可追溯 |
| D9 | `WITH_SOCIAL` 关闭时路由不注册（`#ifdef` 包 METHOD_LIST 段） | 社交控制器文件在 `WITH_SOCIAL=OFF` 时整文件排除编译，本控制器常驻，用宏保持同等语义；治理门按 `WITH_SOCIAL=ON` 口径对账（其路由正则不感知预处理器，与 `/api/github/login` 的既有处理一致） |

## 六、前端设计（user 门户）

**结论：补充前端**（回答上游「前端页面是否要补充」——要，否则 B2 缺自助门户入口，仍是「API 有、用户用不到」的半成品）。

1. `SecurityPage.vue` 新增「Connected accounts」卡片（与 Password/MFA/WebAuthn 并列）：
   - 渲染 `GET /api/me/social/links` 列表（provider 徽标 + 关联时间 + Unlink 按钮）；
   - Unlink → `window.confirm` 确认 → `DELETE` → 刷新列表；409 守卫错误经 `normalizeError` 展示；
   - 「Link GitHub」按钮：仅当 `VITE_GITHUB_CLIENT_ID` 存在时显示，authorize URL 与 LoginPage
     同构，但 `state=link`、`redirect_uri` 复用 `/callback/github`（GitHub OAuth App 单
     redirect_uri 约束）。
2. `GitHubCallbackPage.vue` 二分支：`state === 'link'` 分支必须在**既有登录 POST 之前**
   短路（否则 link 流会把来访者静默签成映射用户的新会话）——`state==='link'` 且已登录 →
   POST `/api/me/social/links/github` `{code}` → 成功跳回 `/security`；`state` 缺失或非
   `link` → 走既有登录路径（LoginPage 现有 authorize URL 不带 state，缺失即登录语义）。
3. Google/WeChat 按钮：本期不做（前端无既有 env/按钮基建），API 已就绪，后续加按钮即可。
4. `userService.ts` 加三个方法；错误路径走既有 `errorAdapter`。
5. e2e（`frontends/user/tests/e2e/`，playwright `testDir`）：新增 1 条 spec（SecurityPage
   渲染 connected-accounts 卡片 + `setupMocks` mock list + unlink 交互），复用既有
   `helpers/mock-api` 拦截基建；link 跳转流（window.location 出站）不在 e2e 覆盖范围，与既有
   GitHub 登录 e2e 空缺一致。

## 七、影响范围与联动

| 面 | 影响 | 动作 |
|---|---|---|
| `apps/server/openapi.yaml` | +3 path（GET/POST/DELETE），schemas（SocialLinkEntry、错误 envelope 复用既有） | 手工更新 + `check_spec_governance.py` 过门；oasdiff 纯新增 → 非破坏 |
| 客户端 SDK | YAML 是生成源 | `tools/clients/regen_clients.py` 再生成 python/go 并提交（CI 漂移门） |
| 版本号 | 新增端点 = minor | `cmake/Version.cmake` + `openapi.yaml info.version` + `clients/python/pyproject.toml` 1.2.0 → **1.3.0**（regen `--version-only` 校验联动；tag/release 走既有 release 流程，不在本 PR） |
| OpenApiGenerator 文档注册 | 3 条 `selfServiceEp` | 与 yaml 同步（治理门三层对账） |
| **指纹基线测试** | `tests/integration/concurrency/Property4_OpenApiValidationBaselineTest.cc` 的 `kFingerprint` 冻结串需 +3 条操作 | 治理门与该测试都按它对账，漏改则 AC4 失败 |
| ORM | 无表结构变更 | **无迁移、无 /orm-gen** |
| 审计 | 4 新事件类型 | audit 查询侧（admin dashboard 过滤器）无需改（事件自由字符串） |
| 文档 | iam-architecture-audit §四 P1 行、next-phase-implementation-plan B2 勾选、progress-status | 收尾时更新 |
| CI | static-checks（治理门）、clients-sdk（漂移门）、ctest 矩阵 | 本地全部预跑 |

## 八、安全考量与遗留风险

1. **无服务端 state（D3 遗留）**: 理论上「攻击者诱导受害者浏览器提交攻击者的 code」可把攻击者
   的 GitHub 关联到受害者账号（后续攻击者可用自己的 GitHub 登录受害者账号）。防线：SPA 只在
   SecurityPage 主动发起 link、callback 只接受自己发起的跳转返回；完整缓解（服务端 state +
   TTL 会话）列为后续增强，不在本期。**风险接受理由**：攻击前提是能在受害者浏览器上执行
   任意跳转/scripting，此时受害者会话已失守；且本任务同时给了 unlink 让用户可自查自解。
2. **账号枚举**: 冲突 409 措辞固定为 "already linked to another user/account"，不返回归属方
   任何信息；`AccountUnavailable`（死账号占映射）与正常冲突同措辞。
3. **V024 软删除契约**: 一切用户解析带 `deleted_at IS NULL`；被软删用户的 link 请求 → 404。
4. **锁定用户**: link/unlink 不检查 `locked_until`（管理面已可通过 lock 阻断登录；自服务凭证
   管理对锁定用户开放与 Keycloak 一致；unlink 守卫只关心「锁死」不关心「锁定」）。——若评审
   认为应拒，改为增加 LockedCheck（成本 1 个 findBy）。
5. **subject 长度**: `subject VARCHAR(128)`；GitHub 数字 id / Google sub / WeChat openid 均
   远小于 128，服务端不截断、超长直接 ExchangeFailed（provider 数据异常）。
6. **provider 响应过滤**: 复用各 AuthService 既有字段白名单，不引入新的 provider 原始数据落库。

## 九、测试策略

| 层 | 文件 | 覆盖 |
|---|---|---|
| 单元（identity） | `libs/identity/test/SocialLinkServiceTest.cc`（新） | 3 provider link happy、InvalidProvider、ExchangeFailed、AlreadyLinkedToSelf/Other、ProviderConflictForUser、insert 竞态 Conflict、unlink happy/NoLink/LastCredentialGuard（有密码放行/无密码拦截）、list、repo 错误传播 |
| 单元（repo fake） | `FakeSocialAccountRepository` 扩展 + 既有 `SocialAuthServiceTest` 不回归 | 新 4 方法内存语义 |
| HTTP 集成 | `tests/integration/controllers/SocialLinkEndpointHttpTest.cc`（新，SocialMockFixture 注入 fake-backed SocialLinkService） | PG 腿（`postgresAvailable() && serverReachable()` 守卫，与 `UserSelfServiceEndpointHttpTest` 同款——memory 腿无法产出能过 OAuth2AuthFilter 的 token，GitHub 假登录签发的 token 是明文存储，`validateAccessToken` 按 hash 查永远 miss）：401、400 bad provider、400 missing code、happy link→list→unlink 闭环、409 三态、404 NoLink、守卫 409、交换失败 502 |
| 全量 | `full_test.bat` 8 步 + 前端 admin/user | 既有回归 |
| 前端 | `frontends/user` build + e2e + unit | 新卡片渲染/unlink 交互 |

## 十、明确不做（本期排除）

- Google/WeChat **登录**消费映射（登录对齐，后续项）；
- 服务端 state 会话（D3/§八.1，后续增强）；
- `(provider, internal_user_id)` DB 唯一约束迁移；
- 管理端（admin）查看/解除他人社交映射的 API；
- 前端 Google/WeChat link 按钮；
- link 时合并既有账号资料（email 回填等）。
