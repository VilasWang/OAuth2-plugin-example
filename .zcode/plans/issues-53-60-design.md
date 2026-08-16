# 设计方案：issues #53 / #54(执行部分) / #56 / #58 / #59 / #60

> 状态：待评审
> 基线：HEAD a9a1bf8（所有 issue 已在当前 HEAD 逐一复核属实）
> 第一性原理：正确性 + RFC 规范。凡有客观正确答案的（契约一致性、崩溃、验证语义）不入决策类。

## 0. Issue 分类结论

**非决策类（本 PR 全部实施）**：#53、#56、#58、#59、#60（全部 4 项）、#54 的"执行"部分（deleted/locked 过滤 + 拒绝发 token）。
理由：这些都有 spec/契约驱动的唯一正确解——V024 契约"deleted 用户不得再登录、从所有查询中排除"；db-operations 规则（SIGABRT 防护）；HTTP 验证语义（类型错误必须 400 而非静默成功）。

**决策类（最后问用户，本 PR 不实施其中需要拍板的部分）**：
- D1（#54 衍生）：soft-delete 后 `oauth2_subject_mappings` 的生命周期语义——保留 mapping+拒绝登录（现状推荐，可逆）vs 删除 mapping+允许 GitHub 身份重建新账号。本 PR 实施前者（它是两种语义的公共子集：两种都需要 deleted_at 过滤），后者若用户选择则作为后续小改动。
- D2（审计衍生）：`users_username_key` 全局唯一约束是否改为 partial unique index（`WHERE deleted_at IS NULL`），允许删除后用户名复用。本 PR 不改约束，只在 upsert 中防止"收养"已删除行。

---

## 1. #53 [High] updateUser 未防护的 JSON 强转可致进程 SIGABRT

### 现状（已验证）
- `UserAdminService.cc:803-814`：`email`/`email_verified`/`username` 只有 `isMember` 存在性检查，`asString()/asBool()` 在 findOne 异步回调内裸调用，`Json::LogicError` 逃逸到事件循环 → SIGABRT。
- `createUser` `:510-514` 同类（同步路径，`get("username","").asString()` 在成员存在但类型错时抛）。
- 相邻字段（mfa_enabled/locked/org_id）类型错时静默跳过仍返 200（与 #59-3 同源）。

### 方案
1. **前置类型校验（fail-fast，400）**：updateUser/createUser 在触碰 DB 前，对所有提供的字段做类型断言：
   - `username`/`email` 必须 `isString()`；`email_verified`/`mfa_enabled`/`locked` 必须 `isBool()`；`org_id` 必须 `isInt() || isNull()`（null 语义见 #59）。
   - 违反 → `VALIDATION_INVALID_INPUT` 400（复用 `ErrorResponder::respondValidation` 或现有 respondError）。
   - 消灭"静默跳过仍 200"：类型错一律 400，与 #59-3 统一。
2. **纵深防御**：updateUser 的 findOne 成功回调体外层包 try/catch → `(*cb)(errorResponse)`（db-operations 规范要求 2），保证未来新增强转也不会崩进程。
3. createUser 同样前置校验（username/password/email isString、email_verified/mfa_enabled isBool、org_id isInt/isNull、roles 数组内元素 isString 否则 400）。

### 验收
- PUT `{"email":{"a":1}}` → 400 且进程存活；`{"email_verified":"yes"}` → 400；`{"username":123}` → 400；`{"locked":"yes"}` → 400（原先 200 静默跳过）。
- POST createUser 同类畸形体 → 400。
- 回调内任何强转异常都被捕获并回调错误（代码评审项）。

## 2. #54 [High] soft-delete 用户仍可经 social/MFA 路径获得新 token（执行部分）

### 现状（子代理审计已验证，比 issue 更广）
生产实际路径是 WITH_SOCIAL 服务路径（IdentityAssembly 注入），且：
1. `PostgresSocialAccountRepository::findLinkedUser`（:42-43）按 id 查 users 无 deleted 过滤；**错误回调（:50-55）对不存在用户伪造成功**（username="user"）。
2. `GitHubController::linkExistingUser`（:513-517）fallback 路径同样无过滤且无条件发 token。
3. 三处 `INSERT ... ON CONFLICT (username) DO UPDATE`（GitHubController:552、PostgresSocialAccountRepository:99、PostgresIdentityRepository:699 createUserForExternalLogin）可"收养"已软删行（username 唯一约束是全局的）。
4. `MfaController` fallback 路径（:177/:319/:451/**:848 verifyLogin 完成登录**）无 deleted 过滤；wired 路径经 repo 已过滤。
5. `UserSelfServiceController` 全部 5 个查找（:99/:175/:353/:467/:578）无 deleted 过滤；`deleteAccount` 只匿名化**从不设置 deleted_at**（V024 契约缺口）。
6. `PostgresIdentityRepository::getInternalUserId`（:571）只查 mapping 不验用户存活性（供养 consent→code→token 链）。
7. **吊销键错位**：deleteUser 用 public_sub 吊销 token，但 GitHub 发的 token `user_id` 存内部 id → GitHub token 永远吊销不掉（并入 #56 修复）。

### 方案
**A. 服务路径（生产主路径）**
- 扩展 `ISocialAccountRepository::findLinkedUser` 回调契约，显式区分四态（当前 `optional` 把"无 mapping→建新号"与"DB 错误→也当无 mapping 走建号"混为一谈，且无法表达"有 mapping 但用户不可用→拒绝"）：
  ```cpp
  enum class SocialLinkStatus { Linked, NoMapping, AccountUnavailable, RepositoryError };
  // LookupCallback = std::function<void(SocialLinkStatus, const SocialAccountLookup &)>
  // AccountUnavailable/RepositoryError 时 lookup 为空壳；NoMapping → 上层走 createLinkedUser
  ```
  同步修改 FakeSocialAccountRepository（加 `std::set<std::string> unavailableKeys` 测试开关）、GitHubAuthService、SocialMockFixture 相关用例。
  现状参照：Fake 在 tests 与 libs/identity/test 三处使用，全部在树内可改；SocialLoginHttpTest 的 `injectGitHubFake()` 正是 #54 验收测试（AccountUnavailable → 401 无 token）的注入点。
- **[评审 F2] 两级查找（mapping、users）均改用 `findBy`（vector）而非 `findOne`**：Drogon `findOne` 的错误回调同时承载"零行"与"DB 异常"，无法区分。findBy：空 vector → NoMapping（mapping 查）/ AccountUnavailable（users 查）；`DrogonDbException` → RepositoryError（→ `DB_QUERY_ERROR`，不得降级为 401）。users 查询 criteria 加 `deleted_at IS NULL`。
- **[评审 F1，升级为必修] 三处 upsert 由 `ON CONFLICT (username) DO UPDATE` 改为 `ON CONFLICT (username) DO NOTHING`**：DO UPDATE 不仅能收养已删行（加 WHERE 可挡），还能收养**活跃行**——本地已注册 `gh_alice` 的用户会被 GitHub 登录名为 `alice` 的攻击者整体接管（upsert 返回受害者 id → 建立 attacker_subject→victim_id 映射 → 以受害者身份拿 token）。fail-closed：冲突（无论对方行存活与否）→ RETURNING 空 → 登录失败。孤儿用户名边界（行存在但无 mapping）同样失败——安全优先于可恢复性。`RETURNING` 空结果的判断在索引 `Result[0]` **之前**（`Result::operator[](0)` 对空结果是 UB 而非异常）。
- `GitHubAuthService::login`：`AccountUnavailable` → `result.errorCode = "AUTH_INVALID_CREDENTIALS"`（401，通用错误避免账号状态枚举）；`RepositoryError` → `DB_QUERY_ERROR`（不再错误地走建号分支）。
- 三处 upsert（GitHubController:552、PostgresSocialAccountRepository:99、PostgresIdentityRepository:699 createUserForExternalLogin）按 §2.A 统一改 `DO NOTHING` + 空 RETURNING 判断（**[评审 F1]** 同时封堵活跃行接管与已删行收养；`DO UPDATE SET username=users.username` 的 createUserForExternalLogin 同属接管向量）。

**B. fallback 路径（GitHubController）**
- `linkExistingUser`：criteria 加 `deleted_at IS NULL`（findBy 空结果 → `AUTH_INVALID_CREDENTIALS`，与 findOne 空结果同路径）；取到行后检查 `locked_until > now` → 锁定同样拒绝。
- `createNewLinkedUser`：upsert 改 `DO NOTHING` + 空 RETURNING → 登录失败错误（同 §2.A；索引前判空）。

**C. MFA fallback**
- :177/:319/:451 查找加 `deleted_at IS NULL` → 404。
- :848 verifyLogin：加过滤 → 已删用户 MFA 验证失败（401，MFA_CODE 语义）；并检查 `locked_until`（锁定用户不得完成登录，与密码路径一致）。wired 路径已由 repo 过滤 deleted；verifyLogin wired 路径同样补 locked 检查（repo 返回的 UserData 含 lockedUntil）。

**D. UserSelfServiceController**
- 5 个查找全部加 `deleted_at IS NULL` → `VALIDATION_RESOURCE_NOT_FOUND` 404（V024 "excluded from all queries" 契约）。各端点当前错误语义保持：DB 异常回调维持 `DB_QUERY_ERROR`，仅"用户已删/不存在"映射 404；`listAuthorizedApps` 空结果仍 200（空 apps 列表），仅用户级 404 前置。
- `deleteAccount`：匿名化之外**设置 `deleted_at`**（自删=软删，V024 语义），token 吊销键同时覆盖 public_sub 与内部 id（同 #56）。**[评审 F3]** 若行为者是最后一个活跃 admin → `VALIDATION_RESOURCE_CONFLICT` 409（复用 isLastActiveAdmin，否则 last-admin 守卫可被 /api/me DELETE 绕过）。
- 附注（预存在，不在本 PR 修）：`deleted_<秒级时间戳>` 匿名化在同一秒内两个自删可撞全局唯一 username 约束。

**E. getInternalUserId（consent 链防御）**
- mapping 解析后追加 users 存活检查（第二次查询，无 JOIN）：`id = internal_user_id AND deleted_at IS NULL` → 不存活按 nullopt（未链接）处理。已确认 SessionController.cc:957 对 nullopt 走 `INTERNAL_ERROR "consent: failed to get user mapping"`（不会尝试建号），行为终点安全。locked 不在此检查（会话语义超出本 issue，注释说明）。

**F. mapping 生命周期（决策项 D1 的默认实现）**
- 保留 mapping（soft-delete 可逆），登录/consent 经上述过滤拒绝。代码注释写明语义："mapping 保留以支持恢复；删除用户的社交登录被拒绝（AccountUnavailable）"。

### 验收
- 集成测试：直插 users+oauth2_subject_mappings 已删用户 → `findLinkedUser` 返回 AccountUnavailable（repo 级集成测试，Postgres 下可做）。
- 集成测试：软删用户调 `/api/me/profile` → 401（token 已被吊销）或 404（防御层），**绝不是 200+数据**。
- upsert：预置同名已删用户 → createLinkedUser → nullopt（不收养）。
- GitHubAuthService 单元（Fake repo）：AccountUnavailable → errorCode 非空。
- 全量测试无回归。

## 3. #56 [Medium] deleteUser token 吊销 fire-and-forget + 错误全吞

### 现状（已验证）
`:930-947` 两条 `execSqlAsync` 成功/错误回调均为空 lambda，外层 `catch (...) {}`，200 先于吊销返回；且 `WHERE user_id = publicSub` 匹配不到 GitHub 发的 token（存内部 id）。

### 方案
1. **串行等待两条 UPDATE 再响应**（access → refresh 链式），每层错误回调 `LOG_ERROR` + catch 记日志。
2. **吊销键覆盖两种标识**：`WHERE user_id = $1 OR user_id = $2`（publicSub、std::to_string(内部 id)）。
3. **响应如实**：soft-delete 本身已成功（200 是事实），但吊销失败时响应体加 `"tokens_revoked": false` + warning 文本 + LOG_ERROR + **audit outcome 记为 `partial`**（[评审 F10] 运维可观测）；成功时 `"tokens_revoked": true`、audit `success`。不返回 500——删除已生效，客户端无法有意义地重试（再删会 404）。
4. 保留 documented-batch raw-SQL 豁免注释（现有），补注双键原因。

### 验收
- 集成测试：创建用户→密码登录拿 refresh token→DELETE 用户→用旧 refresh token 调 /oauth2/token/refresh → 必须失败（真正吊销，而非仅响应成功）。
- DELETE 响应体含 `tokens_revoked: true`。
- 强制吊销失败的注入测试不可行（需破坏表），该路径以代码评审覆盖（LOG_ERROR 存在、响应含 false 标志）。

## 4. #58 [Medium] 用户搜索大小写敏感 + 注释谎言

### 现状（已验证）
`:301-310` LIKE（PG 下大小写敏感）+ 注释宣称 lowercase；e2e mock 用 `toLowerCase().startsWith`（大小写不敏感）→ 生产与 CI 行为分叉。

### 方案
- 用 Criteria 列名位置放表达式（已验证 Drogon `Criteria(colName, op, arg)` 对 colName 原样拼接、无校验，属 Criteria API 合法使用，不触发 raw-SQL 豁免）：
  ```cpp
  Criteria("lower(username)", CompareOperator::Like, asciiToLower(escaped) + "%") ||
  Criteria("lower(email)",   CompareOperator::Like, asciiToLower(escaped) + "%")
  ```
- `asciiToLower`：逐字符 `std::tolower(static_cast<unsigned char>(c))`（**[评审 F7]** 直接对负值 char 调 tolower 是 UB，MSVC debug 下可 assert——不复制 EmailNormalizer.h:39 的潜在缺陷）。非 ASCII 大小写折叠不支持——注释写明（username/email 实际字符集 ASCII；mock 的 JS toLowerCase 对 ASCII 行为一致）。
- 修正注释为真实行为："ASCII case-insensitive prefix match via lower() on both sides"。
- mock 不变（已是不敏感语义，现在生产与 mock 一致）。
- 性能注记：`lower(col) LIKE` 无法用普通索引；admin 列表规模下可接受，注释注明（真需要时上函数索引/pg_trgm，超出本 issue）。

### 验收
- 集成测试：创建 `CaseProbe_<suffix>` 用户，`?q=caseprobe_<suffix>` 与 `?q=CASEPROBE_<suffix>` 均能搜到。
- 代码注释与实现一致（评审项）。

## 5. #59 [Medium] org_id：NULL 渲染 0、不可清除、类型错静默成功

### 现状（已验证）
getUser `:726` 用 `getValueOfOrgId()`（NULL→默认 0）；updateUser `:825` `isInt` 守卫静默跳过 `""`/`null`；无 `setOrgIdToNull` 路径；前端 `UserDetailPage.vue:48,73` 清空时发送 `""`。

### 方案
- **序列化**：getUser 改用可空访问器——`row.getOrgId()` 为 null → JSON `null`，否则整数值。
- **更新语义**（与 #53 前置校验合并实现）：
  - `org_id` 为 int → `setOrgId(v)`；
  - `org_id` 为 JSON null → `setOrgIdToNull()`（显式清除）；
  - 其它类型 → 400。
- **createUser**：`org_id` int → 设置；null/缺省 → 不设置（保持 NULL）。
- **前端 UserDetailPage.vue**：org 输入框空 → diff 时发送 `org_id: null`（清除）；非空时发送 `Number()`；非数字输入前端拦截提示（不发包）。回显 `resp.data.org_id ?? ''` 已正确（后端现在返回真 null）。
- **mock-api.ts**：PUT 处理器同步语义（null → 清除 org 字段；模拟 400 on 类型错误可选，最低要求不回归）。

### 验收
- 集成测试：创建无 org 用户 → GET `org_id` 为 JSON null（非 0）。
- PUT `{"org_id": 5}` → GET 5；PUT `{"org_id": null}` → GET null；PUT `{"org_id": "abc"}` → 400。
- 前端：编辑页清空 org 保存 → 重新加载显示空（e2e，mock 同步后）。

## 6. #60 [Low] 评审残留 4 项 + Mapper 卫生

### 6.1 createUser 角色分配失败静默 201
**方案**：响应体如实化——201 保持（用户确实已建），新增：
- `roles_assigned`: [成功插入的角色名]
- `roles_failed`: [解析失败或插入失败的角色名]
- `roles_failed` 非空时 `LOG_ERROR` + 响应体加 `warning` 字段。
默认角色（"user"）解析/插入失败同样计入 roles_failed。
**验收**：POST `{"roles":["nonexistent_x"]}` → 201 + `roles_assigned:[]` + `roles_failed:["nonexistent_x"]`；正常创建 → `roles_assigned` 含 "user"。

### 6.2 last-admin 锁死防护
**现状**：自删守卫有；删他人/禁用/锁定最后一个活跃 admin 无守卫；assignUserRoles 可摘除最后 admin 的角色；**[评审 F3]** `/api/me` DELETE（deleteAccount）同样可清掉最后 admin。
**方案**：新增辅助函数 `isLastActiveAdmin(db, targetUserId, onResult, onError)`（无 JOIN 三查：roles 里 name='admin' → user_roles(role_id) → 其它 users(id In, deleted_at IS NULL, **(locked_until IS NULL OR locked_until <= now)** 计数——locked_until 可空，评审 F8）：
- `deleteUser` / `disableUser` / `updateUser{locked:true}` / `deleteAccount`（自删）：目标是活跃 admin 且无其它活跃 admin → `VALIDATION_RESOURCE_CONFLICT` 409。
- `assignUserRoles`：目标当前是活跃 admin、新角色集不含 admin、无其它活跃 admin → 409。
- "活跃" = 未删 + 未锁（NULL locked_until 视为未锁）。自删守卫（admin API 的）保留。
- **[评审 F12] 已接受的竞态**：两个并发 last-admin 操作可同时通过计数检查（TOCTOU）。本架构无锁基建，风险记录于此：单管理员场景下管理面双并发操作属极端情形，且 409 防护覆盖绝大多数路径。
- **[评审 F4] 既有测试适配**：AdminUserApiHttpTest 现有 disable/enable 用例操作种子 admin（测试库唯一活跃 admin）→ 加守卫后必 409。适配：这些用例先创建+提升一个临时第二 admin（RAII 清理），或在非 admin 一次性用户上执行。
- 附注：`?locked=false` 列表过滤 `locked_until < now` 会排除 NULL 行——预存在怪癖（NULL 实际不会发生，列默认 0），记录不修。
**验收**：集成测试（见计划文档测试节——用直插 SQL 制造"唯一活跃 admin"场景测 409，RAII 恢复种子 admin 角色）。

### 6.3 openapi.yaml PUT body 漂移
**方案（[评审 F9] 覆盖全部受影响操作）**：
- `PUT /api/admin/users/{id}`：requestBody 补 `username`(string)/`mfa_enabled`(bool)/`locked`(bool)/`org_id`(integer, nullable)；responses 补 400（类型错）/409（last-admin）；description 更新。
- `DELETE /api/admin/users/{id}`、`PUT .../disable`、`PUT .../roles`：responses 补 409；DELETE 200 响应说明 `tokens_revoked` 字段。
- `POST /api/admin/users`：description 注明 201 响应含 `roles_assigned`/`roles_failed`/`warning`。
用 openapi-update skill 流程；跑 tools/api-diff 基线比对。
**验收**：openapi 与实现字段一致（api-diff 通过）；五个操作的 409 均有记载。

### 6.4 误导性安全注释
**方案**：`TokenEndpointController.cc:1926-1929` 改写为"handler 检查是**唯一**执行点（registry 刻意排除 /oauth2/userinfo，见 ResourceScopeRegistry.cc isAuthGatedPath）——不可删除"；`ResourceScopeRegistry.h:86-88` 文档同步（(b) 项家族列表去掉 userinfo 或注明其 handler-exclusive）。
**验收**：注释与 registry 实际行为一致（评审项）。

### 6.5 Mapper 构造 try/catch 卫生（#60 "also noted" + 本次自查扩大）
**现状**：UserAdminService.cc 内 `fetchUserRoleNames`(:459,:474)、`getUser`(:706)、`disableUser`(:1001,:1007)、`enableUser`(:1056,:1063)、`getUserRoles`(:1116,:1134)、`assignUserRoles`(:1221,:1237,:1270) 的 Mapper 构造无独立 try/catch（db-operations 强制要求，含异步回调内的）。
**方案（[评审 F5] 范围 = 本 PR 触碰的所有函数）**：逐一包裹 try/catch → 错误回调。覆盖：
- UserAdminService.cc 上述全部 + 新增的 isLastActiveAdmin 辅助查询。
- PostgresSocialAccountRepository.cc（:34,:41,:112,:119 及新代码）。
- MfaController.cc 本 PR 触碰的 fallback（:177,:319,:451,:848）。
- UserSelfServiceController.cc 本 PR 触碰的全部查找。
- PostgresIdentityRepository.cc 的 getInternalUserId 新增 users 查询。
**验收**：评审项——上述所有 Mapper 构造均在 try 块内且 catch 调用错误回调。

## 7. 不做的事（明确出界）
- username 唯一约束改 partial index（D2 决策项）。
- 删除 oauth2_subject_mappings（D1 决策项；本 PR 保留+拒绝）。
- 搜索性能优化（函数索引/pg_trgm）。
- 会话（cookie）在软删后的存活语义（独立问题，PR 中记录观察）。
- **[评审 F16]** 已删用户现存未吊销 token 在"只读 token 行、不回查 users"的端点（introspection/refresh/userinfo 的 subject 复制语义）上的残余面——本 PR 以串行吊销 + tokens_revoked 如实标志缓解；彻底封堵（吊销失败重试/定期对账）超出范围，记录为后续观察。
- `?locked=false` 列表过滤对 NULL locked_until 行的排除（预存在，NULL 实际不可发生）。
- V006 dev-DB 漂移（issue 明示 not actionable）。

---
## 评审记录
- 第一轮（feature-dev:code-architect）：PASS-WITH-CONDITIONS。1 BLOCKER（upsert 活跃行接管→已采纳：DO NOTHING fail-closed）、4 MAJOR（findOne 语义/findBy 改造→采纳；deleteAccount 绕过→采纳；既有 disable 测试回归→采纳；Mapper 卫生范围→采纳）、4 MINOR（空 RETURNING UB、tolower UB、locked_until NULL、openapi 范围→全部采纳）、7 NOTE（audit partial、竞态记录、错误映射细化等→采纳）。

## 8. 测试与验证总纲
- 后端：AdminUserApiHttpTest.cc 扩展 + 新增 social/MFA/selfservice 定向集成测试（Postgres 环境）；full_test.bat 8 步流水线。
- 前端：mock 同步 + e2e（org 清除用例）+ 既有 16+8 e2e + 单元/属性测试全过。
- openapi：api-diff 基线比对。
