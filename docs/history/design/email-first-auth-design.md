# 技术方案：email 优先的用户认证

**状态**：待评审
**日期**：2026-06
**作者**：authforge 团队
**关联**：本方案基于已合并的 `feat(auth): validate and normalize email on registration`（邮箱格式校验 + Gmail 归一 + email 唯一索引）

---

## 1. 背景与目标

### 1.1 现状问题

当前注册/登录以 **username** 为核心标识：

- 注册：`username`（必填、`VARCHAR(50) UNIQUE NOT NULL`）、`email`（历史可选）
- 登录：仅按 `username` 查询（[AuthService.cc:26-30](../../OAuth2Server/AuthService.cc)）
- 前端登录页字段固定为 username

这不符合 AI 时代主流身份系统的实践（Google / GitHub / Auth0 / Supabase 均以 email 为主标识），用户需记忆两套凭证，且 email 未能承担其应有的"主登录键"角色。

### 1.2 目标

演进为 **email 为主、username 可选、登录支持 email 或 username**：

| 能力 | 目标 |
|------|------|
| 注册 | email 必填且唯一；username 可选（展示用途，不强制唯一） |
| 登录 | 输入框接受 email 或 username，后端自动识别 |
| OAuth2 sub | 继续用 `public_sub`（UUID），与登录键解耦（零影响） |
| 存量兼容 | 现有 `username='admin'` 等账号可继续登录 |

### 1.3 非目标

- ❌ 不删除 username 字段（保留以兼容存量与企业 SSO 场景）
- ❌ 不引入 magic link / OTP 无密码登录（独立后续方案）
- ❌ 不改 OAuth2 token 签发链路（sub 已解耦，无需动）

---

## 2. 关键设计决策

### 2.1 登录标识识别策略

用户在登录框输入一个字符串，后端如何判断是 email 还是 username？

**方案：基于 `@` 字符分流**（业界通用，简单可靠）

```
输入 → trim → 是否含 '@'？
  是 → 按规范化后的 email 查（先 normalizeEmail，再 findOne by email）
  否 → 按 username 查（findOne by username）
```

- 选用 `@` 而非正则的原因：email 必含 `@`，username 的字符集 `^[a-zA-Z0-9_]{1,100}$` **不允许 `@`**（见 `USERNAME_PATTERN`），二者天然互斥，无歧义。
- email 查询前必须 `normalizeEmail`（Gmail 别名折叠），否则 `user.x@gmail.com` 和 `userx@gmail.com` 会被当成两个账号——与注册时的归一逻辑保持一致。

**风险与对策**：若未来放开 username 字符集（允许 `@`），此分流失效。决策记录在此，未来放开 username 字符集时必须重新设计识别策略。

### 2.2 username 唯一性如何处理

**冲突点（Convention Beats Novelty）**：

- 当前 `username` 是 `UNIQUE NOT NULL`（[V004](../../OAuth2Server/sql/migrations/V004__users_table.sql#L5)）
- 目标是 username 可选（可空）

**决策：username 仍保留 UNIQUE，但改为可空**

- `UNIQUE` 保留：避免两个用户同名造成展示混淆；且 `NULL` 在 PostgreSQL 不参与唯一约束（多个 NULL 允许），与"username 可选"语义兼容
- `NOT NULL` 放宽为可空：未填 username 的用户存 NULL
- 长度上限从 `VARCHAR(50)` 扩到 `VARCHAR(100)`：与 `USERNAME_PATTERN` 的 `{1,100}` 对齐（当前 ORM 校验允许 100，但 DB 列只有 50，已存在不一致——借此修正）

### 2.3 双重校验冲突的收口

**冲突点（必须决策）**：

| 机制 | password 校验 | username 校验 | 是否在登录链路生效 |
|------|--------------|--------------|-------------------|
| `RuleSet::login` | 仅长度 8-200 | 仅长度，无正则 | ✅ 生效（SessionController 调用） |
| `RequestValidationFilter` | 长度 + `PASSWORD_PATTERN` 正则 | `USERNAME_PATTERN` 正则 | ❌ 未挂载到 `/oauth2/login` 路由 |

**决策：登录链路以 `RuleSet` 为准，`RequestValidationFilter` 不挂载**

理由（与上次邮箱校验的收口一致）：
1. SessionController 现在实际调用的是 `RuleSet`，改动连贯
2. `RequestValidationFilter` 当前未挂载到任何登录/注册路由（仅测试代码引用），挂载它属于独立较大改动
3. 两套规则不一致（正则 vs 仅长度），同时启用会产生"filter 先拦"的混乱

> **遗留（评审已决策：不补）**：`RuleSet::login` 的 password 校验无正则，**本次也不补**。评审结论：登录应遵循"最宽容验证"原则——正则限制仅用于注册/改密码，避免未来升级密码强度策略时锁死存量弱密码用户。

### 2.4 存量数据与 seed 兼容

**admin 账号**：`dev_admin_user.sql` 按 `username='admin'` 建号，email 为 `admin@example.com`。

**决策：admin 账号补全 email 后，登录链路天然兼容**
- admin 的 email 字段已存在（`admin@example.com`），无需改 seed
- 登录既支持 `username=admin` 也支持 `email=admin@example.com`，存量测试代码（`username=admin&password=admin`）继续可用
- **不改 seed 文件的 `WHERE username='admin'`**：seed 是一次性建号脚本，按 username 定位 admin 是稳定的（admin 永远存在 username）

---

## 3. 改动范围（按层）

### 3.1 数据库层

新增 migration `V020__username_optional.sql`：

```sql
-- username 放宽为可选（保留 UNIQUE，NULL 豁免）
ALTER TABLE users ALTER COLUMN username DROP NOT NULL;
-- 长度上限对齐 ORM（50 → 100）
ALTER TABLE users ALTER COLUMN username TYPE VARCHAR(100);
-- CHECK 约束放宽：非空时仍不允许空串
ALTER TABLE users DROP CONSTRAINT IF EXISTS users_username_check;
ALTER TABLE users ADD CONSTRAINT users_username_check
    CHECK (username IS NULL OR username <> '');
```

**前置数据检查**：现有 username 均非空且 ≤100 字符（V004 限定 50），迁移无阻塞。

> **注意**：放宽 `NOT NULL` 后，应用层（注册逻辑）必须保证"email 必填"，否则会出现既无 username 又无 email 的账号。见 3.3。

### 3.2 校验层（RuleSet）

**`RuleSet::login`**（[RuleSet.cc:389](../../OAuth2Plugin/src/validation/RuleSet.cc#L389)）改为接受"登录标识"：

- 新增可选 `email` 字段解析（与 username 二选一）
- 校验规则：`username` 与 `email` **不能同时为空**；email 非空时走 `EMAIL_PATTERN`
- 移除"username 必填"硬约束

**`RuleSet::registerUser`**（[RuleSet.cc:435](../../OAuth2Plugin/src/validation/RuleSet.cc#L435)）：

- `email` 改为**必填**（与"email 为主"目标一致）
- `username` 改为**可选**（非空时校验长度+正则）

### 3.3 服务层（AuthService）

**`AuthService::validateUser`**（[AuthService.cc:16](../../OAuth2Server/AuthService.cc#L16)）：

当前只按 `_username` 查。改为：

```
1. 从请求取登录标识（identifier）+ password
2. identifier 含 '@' → normalizeEmail → findOne by email
                 否 → findOne by username
3. 查到后流程不变（锁定检查、密码校验、失败计数）
4. 查不到 → 统一返回"用户名或密码错误"（防枚举，见 2.5）
```

**`AuthService::registerUser`**（[AuthService.cc:170](../../OAuth2Server/AuthService.cc#L170)）：

- email 必填校验（已在 RuleSet 做，AuthService 作为兜底）
- username 可选：仅 `if (!username.empty())` 才 setUsername（当前已是此逻辑 ✅）

**`json["name"]` 的 email fallback**（评审新增需求）：

OIDC `name` claim 在 username 可空后可能缺失，严格 OIDC 客户端会报错。两处生成点改为"username 优先、为空回退 email"：

- [AuthService.cc:297](../../OAuth2Server/AuthService.cc#L297) 和 [AuthService.cc:316](../../OAuth2Server/AuthService.cc#L316)（`getUserInfo`）：`name = username.empty() ? email : username`
- [OAuth2StandardController.cc:1571](../../OAuth2Plugin/src/controllers/OAuth2StandardController.cc#L1571)（OIDC userinfo）：从 `dbUserInfo["username"]` 取——getUserInfo 做 fallback 后此处透传；dbUserInfo 无 username 分支用 email 兜底

**`SessionController::login`**（[SessionController.cc:320](../../OAuth2Server/controllers/SessionController.cc#L320)）：

- 解析参数：从 `username` 字段读"登录标识"（前端继续用 username 字段名提交，兼容；或新增 `identifier` 字段，见 3.4）
- 调用 `validateUser` 时传入标识

### 3.4 前端层

**登录页字段语义**（4 处）：

| 位置 | 改动 |
|------|------|
| [OAuth2Frontend/.../LoginPage.vue](../../OAuth2Frontend/src/pages/auth/LoginPage.vue) | label `Username` → `Email or Username`，placeholder 改 `you@example.com` |
| [OAuth2Admin/.../LoginPage.vue](../../OAuth2Admin/src/pages/LoginPage.vue) | 同上 |
| [authService.ts](../../OAuth2Frontend/src/services/authService.ts) | 函数签名 `login(username, ...)` 语义注释为"登录标识"，不改字段名（后端兼容） |
| [login.csp](../../OAuth2Server/views/login.csp) | 同样改 label/placeholder |

**前端字段名决策**：前端继续用 `username` 字段名提交（后端把它当"登录标识"解析），**避免破坏 API 契约**。这是最小改动。

**注册页**（[RegisterPage.vue](../../OAuth2Frontend/src/pages/auth/RegisterPage.vue)）：email 标为必填、username 标为可选（调整 label 和 required）。

### 3.5 测试层

新增/修改测试：

- **新增**：`EmailLoginTest`——email 登录、username 登录、Gmail 别名登录（`user.x@gmail.com` 登录应等于 `userx@gmail.com`）
- **新增**：`RegistrationWithoutUsernameTest`——仅 email 注册成功
- **修改**：现有 e2e/integration 测试的登录用例**保持 `username=admin`**（验证存量兼容），**新增** `email=admin@example.com` 登录用例

### 3.6 不改动的部分（已确认解耦）

- `users.public_sub`（V007）：OAuth2 sub 来源，与登录键无关
- `oauth2_subject_mappings`（V006）：subject 映射到 internal user id
- Token 端点（`OAuth2StandardController`）：不含 password grant，登录链路单一
- `dev_admin_user.sql`：按 username 建 admin 的逻辑保留

---

## 4. 数据流（改动后）

### 4.1 注册流程

```
POST /api/register { email(必填), password, username?(可选) }
  → RuleSet::registerUser 校验（email 必填+格式，username 可选+格式）
  → AuthService::registerUser
    → normalizeEmail(email) 存库
    → username 非空才存
  → DB 唯一约束（email 部分唯一索引、username UNIQUE）
```

### 4.2 登录流程

```
POST /oauth2/login { username: "<登录标识>", password }
  → RuleSet::login 校验（标识与 password 非空）
  → SessionController 解析标识
  → AuthService::validateUser
    → 标识含 '@'? normalizeEmail → findOne(email) : findOne(username)
    → 密码校验 + 锁定检查
  → 成功 → generateAuthorizationCode(public_sub)
```

---

## 5. 安全考量

### 5.1 防枚举

登录失败时，无论"用户不存在"还是"密码错误"，**返回统一错误信息**（"用户名或密码错误"），避免攻击者通过差异判断 email/username 是否注册。

> 当前实现需核查：`validateUser` 在"用户不存在"分支返回的错误信息是否与"密码错误"一致。若不一致，本次一并修正。

### 5.2 email 归一一致性

注册和登录**必须用同一个 `normalizeEmail`**，否则会出现"用 `user.x@gmail.com` 注册、用 `userx@gmail.com` 登录查不到"的故障。本方案两侧都调用 [EmailNormalizer.h](../../OAuth2Plugin/include/oauth2/utils/EmailNormalizer.h)。

### 5.3 时序攻击

"先查用户、再校验密码"的模式存在时序侧信道（用户不存在时返回快）。本次不专门处理（现有架构如此，且已有账号锁定机制兜底），记录为已知风险。

---

## 6. 风险与回滚

| 风险 | 概率 | 影响 | 对策 |
|------|------|------|------|
| 放宽 username NOT NULL 后出现无 username 无 email 的脏数据 | 中 | 账号无法登录 | 注册逻辑强制 email 必填 + 加 DB 约束或应用层断言 |
| email 归一不一致导致登录失败 | 中 | 用户投诉 | 注册/登录共用 normalizeEmail；单元测试覆盖 |
| 存量测试用例失败 | 高 | CI 红 | 保留 `username=admin` 兼容，新增不删旧 |
| migration 在生产数据上失败 | 低 | 部署阻塞 | 迁移前验证 username 均 ≤100 字符（已确认） |

**回滚**：所有改动可独立回滚。migration 用 `ALTER`，可写 `V02x__rollback_username_optional.sql` 反向（但生产慎用）。

---

## 7. 决策记录（评审已确认）

| # | 问题 | 决策 | 理由 |
|---|------|------|------|
| 1 | 登录标识识别方式 | ✅ 用 `@` 分流 | USERNAME_PATTERN 字符集限制下 100% 互斥，性能最高 |
| 2 | username UNIQUE 约束 | ✅ 保留（NULL 豁免） | PostgreSQL UNIQUE 对 NULL 豁免，完美兼容"可选但防重名" |
| 3 | 前端字段名 | ✅ 继续 `username` 传输 | 维持 API 向后兼容，降低外部客户端升级成本 |
| 4 | 登录补 PASSWORD_PATTERN 正则 | ❌ **不补** | 登录最宽容原则：正则仅用于注册/改密码，防止未来升级强度策略锁死存量弱密码用户 |
| 5 | json["name"] fallback | ➕ **新增**：username 空时回退 email | 防止严格 OIDC 客户端因 name 缺失报错 |

---

## 8. 参考资料

- 已合并：邮箱格式校验 + Gmail 归一（`feat(auth): validate and normalize email on registration`）
- OIDC sub 规范：RFC 6749 / OpenID Connect Core 5.7（sub 为不透明唯一标识）
- 主流参照：Auth0、Supabase Auth、AWS Cognito 的 email-first 模型
