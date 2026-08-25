# B2 社交账号 link/unlink — 实施计划

> **设计文档**: [social-link-unlink-design.md](social-link-unlink-design.md)（本计划实现其全部条目）
> **创建日期**: 2026-08-21
> **分支**: `feat/social-link-unlink`（自 master）
> **工作拆分**: 7 个提交点（M1–M7），每个提交点独立可构建

---

## 前置事实（已核实）

- `WITH_SOCIAL` 默认 ON（顶层 CMakeLists L54）。
- `oauth2_subject_mappings` 已有 ORM 模型（`libs/storage-postgres/.../models/Oauth2SubjectMappings.h`），无需 /orm-gen。
- 测试双打 `FakeOAuthHttpClient`/`FakeSocialAccountRepository` 与注入基建 `SocialMockFixture` 已存在。
- 治理门 `python tools/openapi-governance/check_spec_governance.py`（三层对账 + 版本联动）在 CI static-checks。

---

## M1 — identity 层：repo 接口扩展 + 两个实现

**改动**:
1. `libs/identity/include/fulla/identity/ISocialAccountRepository.h`
   - 新增 `SocialLinkEntry{provider, subject, linkedAt}`、`LinkMutationStatus{Inserted, Conflict, Error, Deleted, NoLink}`（枚举语义：insert 用 Inserted/Conflict/Error，delete 用 Deleted/NoLink/Error）；
   - 新增 4 纯虚方法：`listForUser` / `insertLink` / `deleteLink` / `userHasUsablePassword`（签名见设计 §4.3）。
2. `libs/storage-postgres/src|include/.../PostgresSocialAccountRepository.*` — 全 Mapper 实现（无 raw SQL）：
   - `insertLink` 错误回调以 `duplicate key` 子串判定唯一冲突（仓库先例 `PostgresConsentRepository.cc:87`；libpq `what()` 不含 SQLSTATE，不可判 23505）；
   - `deleteLink` 以 affected==0 → `NoLink`；
   - `userHasUsablePassword` 用 `CompareOperator::Like` + `$pbkdf2-sha256$%`。
3. `libs/identity/include/fulla/identity/testing/FakeSocialAccountRepository.h` — 内存实现（vector + 可注入的失败开关，供单测错误路径）。

**验收（M1）**:
- [ ] `build.sh --debug`（或等价 cmake build）编译通过，WITH_SOCIAL=ON。
- [ ] `SocialAuthServiceTest` 既有用例零回归（ctest identity 段）。
- [ ] Fake 新 4 方法行为有单测覆盖（含 Conflict/NoLink/错误注入）。

## M2 — identity 层：GitHubAuthService::fetchProfile 抽取 + SocialLinkService

**改动**:
1. `libs/identity/include/fulla/identity/SocialAuthService.h` + `src/social/GitHubAuthService.cc`:
   - 新增 `GitHubProfileResult{errorCode, githubId, login, email}` 与 `fetchProfile(code, cb)`；
   - `login()` 重构为调用 `fetchProfile` 后接既有 find-or-create（行为不变，错误码不变）。
2. 新建 `libs/identity/include/fulla/identity/SocialLinkService.h` + `src/social/SocialLinkService.cc`（接口与编排见设计 §4.2；按值捕获依赖，禁 `[this]`）。
3. `libs/identity/CMakeLists.txt` — WITH_SOCIAL 段加 `SocialLinkService.cc`。
4. 新建 `libs/identity/test/SocialLinkServiceTest.cc`。

**验收（M2）**:
- [ ] `SocialLinkServiceTest` 覆盖设计 §九单元行全部场景（≥14 用例：3 provider happy、invalid provider、exchange 失败、self/other 冲突、provider 换绑冲突、insert 竞态、unlink happy、NoLink、守卫两种、list、repo 错误）。
- [ ] `SocialAuthServiceTest`（GitHub login 行为）零回归。
- [ ] identity ctest 全绿。

## M3 — drogon 层：控制器 + 装配 + 审计

**改动**:
1. `libs/drogon/include|src/.../UserSelfServiceController.{h,cc}`:
   - `#ifdef WITH_SOCIAL`: 3 条路由（GET `/api/me/social/links`、POST/DELETE `/api/me/social/links/{provider}`）、handler、`setSocialLinkService`、`initApiDocsImpl` 3 条 `selfServiceEp`；
   - handler 顺序 = 设计 §4.5（装配→provider→code→用户解析→service→审计）；用户解析镜像 userinfo numeric dispatch（`userId` 纯数字 → 按 `users.id`，否则按 `public_sub`；均带 `deleted_at IS NULL`），保证 GitHub 社交会话可用；
   - 4 个审计事件（`social_account_linked/link_failed/unlinked/unlink_blocked`）。
2. `apps/server/src/bootstrap/IdentityAssembly.cc` — WITH_SOCIAL 段构造并注入 `SocialLinkService`（进程级 static）。

**验收（M3）**:
- [ ] 编译通过（WITH_SOCIAL=ON 与 =OFF 各一次，OFF 时路由不注册、无符号残留）。
- [ ] 手动起服务（PG）：`curl` 冒烟——401（无 token）/ 400（bad provider）/ 200（list，admin token，空列表）。

## M4 — HTTP 集成测试

**改动**: 新建 `tests/integration/controllers/SocialLinkEndpointHttpTest.cc` + `tests/common/SocialMockFixture.h` 扩展 `injectSocialLinkFake()`（FakeOAuthHttpClient + FakeSocialAccountRepository 支撑的真 `SocialLinkService`，进程级 static 注入 UserSelfServiceController）+ `tests/integration/CMakeLists.txt` 挂载（若按目录自动收集则免）。

**用例**（全部 `postgresAvailable() && serverReachable()` PG 守卫——memory 腿无法产出能过
OAuth2AuthFilter 的 bearer token：GitHub 假登录签发的 token 明文存储，`validateAccessToken`
按 hash 查永远 miss；与 `UserSelfServiceEndpointHttpTest` 的既有口径一致）:
- P1 无 token →401（此用例不需 PG 数据，但守卫保持一致，memory 腿 skip 可接受）。
- P1 bad provider →400；P1 missing code →400。
- P0 admin link github（fake 交换成功）→200→list 含条目→unlink→200→list 空。
- P0 二次 link 同 (provider,subject) →409 self；P0 同 provider 不同 subject（换绑）→409。
- P0（provider,subject）映射到另一用户 →409 other（需第二用户 + 其 token 或手插映射）。
- P0 unlink 未关联 →404；P1 交换失败（fake 返回非 200）→502。
- P1 守卫：手插一条映射 + 把目标用户 `password_hash` 置为非 `$pbkdf2-sha256$` 前缀 → DELETE →409；
  恢复合法前缀 → DELETE →200（SQL 直改测试数据，跑在独立测试库上）。

**验收（M4）**:
- [ ] memory 模式跑本文件：全部用例干净 skip（守卫生效），零失败。
- [ ] PG 模式跑本文件全用例零失败。

## M5 — OpenAPI + SDK + 版本联动

**改动**:
1. `apps/server/openapi.yaml`: +`/api/me/social/links`（GET）、`/api/me/social/links/{provider}`（POST/DELETE）；schemas: `SocialLinkEntry`、`SocialLinksList`、`SocialLinkResult`；`info.version: 1.3.0`。
2. `cmake/Version.cmake` → 1.3.0；`clients/python/pyproject.toml` → 1.3.0。
3. `tests/integration/concurrency/Property4_OpenApiValidationBaselineTest.cc` 的 `kFingerprint` 冻结串 +3 条操作（`GET /api/me/social/links`、`POST /api/me/social/links/{provider}`、`DELETE /api/me/social/links/{provider}`）——治理门与该测试都按它对账。
4. `python tools/clients/regen_clients.py`（python+go 生成物提交）。

**验收（M5）**:
- [ ] `python tools/openapi-governance/check_spec_governance.py` + `--selftest` 通过（三层对账 + 版本联动 + kFingerprint 基线）。
- [ ] Property4 基线测试在改指纹后随 ctest 通过（治理门解析正常）。
- [ ] `regen_clients.py --check` 通过（漂移门）。
- [ ] oasdiff 无 breaking error（CI 门语义：纯新增 path + schema）。

## M6 — 前端（user 门户）

**改动**:
1. `frontends/user/src/services/userService.ts`: `getSocialLinks` / `linkSocialAccount(provider, code)` / `unlinkSocialAccount(provider)`。
2. `SecurityPage.vue`: Connected accounts 卡片（列表 + Link GitHub 按钮（`VITE_GITHUB_CLIENT_ID` 门控，authorize URL 带 `state=link`）+ Unlink（confirm））。
3. `GitHubCallbackPage.vue`: `state==='link'` 分支在既有登录 POST **之前**短路 → 已登录则 link API → 跳 `/security`（实际路由，无 `/account` 前缀）；`state` 缺失 → 登录语义（现 LoginPage 不发 state）。
4. e2e: `frontends/user/tests/e2e/`（playwright testDir）新增 1 个 spec（卡片渲染 + `helpers/mock-api` mock list + unlink 交互）；复用既有 mock 基建。

**验收（M6）**:
- [ ] `npm run build`（user）零错误（tsc 严格）。
- [ ] user e2e 全绿（既有 8 + 新 1）。
- [ ] `npm run test:unit`（5 文件含 3 属性）零回归。

## M7 — 全量验证 + 文档 + PR

**步骤**:
1. `full_test.bat` 全量后端 8 步（DB reset→ORM→build→ctest→server→59→52→teardown）全绿。
2. 前端全量（admin: tsc+build+16 e2e；user: build+e2e+unit）。
3. 文档: `iam-architecture-audit.md` §四 P1 link/unlink 行标注已实现；`next-phase-implementation-plan.md` B2 勾选 ✅（附分支/PR 号）；`progress-status.md` 追加条目。
4. CI 静态检查本地预跑: `check_spec_governance.py`、`regen_clients.py --check`、clang-format 检查（若 CI 有）、actionlint（若改 workflow——本任务不改 workflow）。
5. 发 PR（标题 `feat(identity): social account link/unlink self-service (B2)`），关联 IAM 审计与上游计划；自评审 + 请求评审。

---

## 总验收标准（Definition of Done）

| # | 标准 | 验证方式 |
|---|---|---|
| AC1 | 三端点按设计 §3 语义工作（含全部错误码映射） | M4 HTTP 集成用例全绿 |
| AC2 | 编排逻辑（冲突三态/守卫/竞态兜底）正确 | M2 单测全绿 |
| AC3 | `WITH_SOCIAL=OFF` 构建通过且路由不存在 | M3 手动 OFF 构建验证 |
| AC4 | 治理门 + SDK 漂移门 + oasdiff 本地全过 | M5 命令输出 |
| AC5 | 全量后端 8 步 + 前端全量零回归 | M7 步骤 1-2 日志 |
| AC6 | 前端卡片可用（列表/解绑/跳转 link） | M6 e2e + 构建 |
| AC7 | 版本联动 1.3.0 三处一致 | 治理门 --version-only 语义 |
| AC8 | 文档三处收尾更新 | M7 步骤 3 |
| AC9 | PR 开出且 CI 绿 | PR 状态 |

## 风险与回退

- **风险 1**（竞态映射分类）: `insertLink` 依赖 `duplicate key` 子串判冲突——DB 约束是 `(provider, subject)` 且刻意无 `(provider, internal_user_id)` 约束（D5），所以插入竞态**必然是别的用户抢注了同一 subject**，此时正确语义是 409（AlreadyLinkedToOtherUser）而非 500。若 libpq 错误文案变化导致子串失配，退化行为是 500（服务仍安全，只是语义降级）——PostgresConsentRepository 已依赖同一文案约定，风险共担可接受。
- **风险 2**: e2e mock 模式与既有 8 用例基建不一致 → 已核实 `tests/e2e/helpers/mock-api` 存在且 security.spec.ts 在用；实现时复用其 `setupMocks`/`page.route`。
- **风险 3**: 前端 e2e 环境无 GitHub OAuth 配置 → 卡片按钮按 env 门控，e2e 用 mock 路由不依赖真实 env。
- **风险 4**: 社交会话双键（#54/#56）——numeric dispatch 只救新端点，`/api/me` 其它端点仍 404（既有行为，不在本期扩大）；前端卡片仅依赖新端点，不受影响。
- **回退**: 纯增量特性，revert 单个 merge commit 即回退，无迁移/无 ORM 变更。

## 并行安全

与 open PR #64（competitor-benchmark，仅 benchmarks/）无文件交叉 → 安全。
