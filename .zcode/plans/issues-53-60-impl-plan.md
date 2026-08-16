# 实施计划：issues #53/#54(执行)/#56/#58/#59/#60 批量修复

> 依据：.zcode/plans/issues-53-60-design.md（已过子代理评审，条件全部采纳）
> 分支：`fix/issues-53-60-useradmin-social-hardening`（自 master a9a1bf8）
> 提交切分：按 issue 分 commit，便于评审与回滚

## Phase 1 — 后端核心（libs/）

### C1. #53+#59+#60-1：UserAdminService 验证语义 + org_id + 角色分配如实化
文件：`libs/drogon/src/admin/UserAdminService.cc`
1. 新增文件内辅助：`validateUpdateFields(json, err) -> bool`、`validateCreateFields(...)`（isString/isBool/isInt-or-null 断言，错误经 respondError 400）。
2. `updateUser`：前置校验（username/email isString；email_verified/mfa_enabled/locked isBool；org_id isInt||isNull）；`org_id==null → setOrgIdToNull()`；`isInt → setOrgId`；findOne 回调整体 try/catch（纵深防御）。
3. `createUser`：前置校验 + org_id null 语义；角色分配改记账（shared_ptr 记 assigned/failed，atomic remaining），响应加 `roles_assigned`/`roles_failed`/`warning`，failed 非空 LOG_ERROR。
4. `getUser`：`json["org_id"] = getOrgId() ? Json(*p) : Json::null`。
**验收**：
- [ ] PUT `{"email":{"a":1}}` / `{"email_verified":"yes"}` / `{"username":5}` / `{"locked":"x"}` / `{"org_id":"abc"}` → 400，进程存活
- [ ] POST 畸形体（`{"username":{"x":1},...}`）→ 400
- [ ] PUT `{"org_id":null}` → GET org_id == JSON null；PUT 5 → GET 5
- [ ] POST `{"roles":["ghost_x"]}` → 201 + roles_assigned:[] + roles_failed:["ghost_x"]；正常 → roles_assigned 含 "user"
- [ ] findOne/update 回调体均在 try 块内（评审）

### C2. #56：deleteUser 吊销串行化 + 双键
文件：`libs/drogon/src/admin/UserAdminService.cc`
1. update 成功回调 → 链式 execSqlAsync（access→refresh），各带 LOG_ERROR 错误回调与 catch；两键 `user_id = $1 OR user_id = $2`（publicSub、to_string(id)）。
2. 全部完成后响应：`tokens_revoked: true|false`（false 时加 warning + audit outcome=partial）。
**验收**：
- [ ] 创建用户→password 登录→DELETE→旧 refresh_token 走 /oauth2/token/refresh → 4xx/失败
- [ ] DELETE 响应含 tokens_revoked:true
- [ ] 错误回调有 LOG_ERROR（评审）

### C3. #58：大小写不敏感搜索
文件：`libs/drogon/src/admin/UserAdminService.cc`
1. `asciiToLower`（unsigned char cast）+ `Criteria("lower(username)", Like, ...)` / `Criteria("lower(email)", Like, ...)`；修正注释。
**验收**：
- [ ] 创建 `CaseProbe_<sfx>` → `?q=caseprobe_<sfx>` 与 `?q=CASEPROBE_<sfx>` 均命中

### C4. #60-2：last-admin 守卫
文件：`libs/drogon/include/authforge/drogon/admin/UserAdminService.h`（**[评审 P2]** helper 声明放这里——`authforge::drogon::admin` 命名空间自由函数，两个 TU 共用）、`libs/drogon/src/admin/UserAdminService.cc`（定义 + deleteUser/disableUser/updateUser/assignUserRoles 接入）、`libs/drogon/src/controllers/UserSelfServiceController.cc`（deleteAccount 接入）
1. `isLastActiveAdmin(db, targetId, onDone(bool), onError)`：roles(name=admin)→user_roles(role_id In)→users(id In && deleted_at IsNull && (locked_until IsNull || <=now)) 排除 target 计数>0。
2. 四个写路径接入 → `VALIDATION_RESOURCE_CONFLICT` 409。
**验收**：
- [ ] 集成：临时双 admin 场景删/禁/锁/摘角色最后一个 → 409（直插 SQL 制造唯一活跃 admin，RAII 恢复）
- [ ] 存在另一活跃 admin 时上述操作 → 200/正常
- [ ] 既有 disable/enable 用例适配（先建第二 admin 或改非 admin 目标）后全过

### C5. #54：social/MFA/selfservice soft-delete 执行（**必须原子提交**——接口+全部消费方同一 commit，[评审 P5]）
文件：
- `libs/identity/include/authforge/identity/ISocialAccountRepository.h`（四态枚举 + 回调签名；§2.F mapping 生命周期注释）
- `libs/identity/include/authforge/identity/testing/FakeSocialAccountRepository.h`（unavailableKeys + 新签名 + 占位用户名冲突开关）
- `libs/identity/src/social/GitHubAuthService.cc`（AccountUnavailable→AUTH_INVALID_CREDENTIALS；RepositoryError→DB_QUERY_ERROR）
- `libs/storage-postgres/src/PostgresSocialAccountRepository.cc`（findBy 改造 + deleted/locked 过滤 + upsert DO NOTHING + 空结果判断 + **Mapper 卫生**）
- `libs/drogon/src/controllers/GitHubController.cc`（linkExistingUser 过滤+locked 检查；createNewLinkedUser DO NOTHING+判空）
- `libs/drogon/src/controllers/MfaController.cc`（fallback 4 处过滤 + verifyLogin locked 检查 wired+fallback + **Mapper 卫生**）
- `libs/drogon/src/controllers/UserSelfServiceController.cc`（5 处过滤→404 + **Mapper 卫生**；deleteAccount 设 deleted_at+双键吊销+**保留** C4 的 last-admin 409）
- `libs/storage-postgres/src/PostgresIdentityRepository.cc`（getInternalUserId 存活检查 + **Mapper 卫生**；createUserForExternalLogin DO NOTHING）
**验收**：
- [ ] HTTP（mock 注入）：Fake 返回 AccountUnavailable → /api/github/login 401 且无 access_token
- [ ] Postgres 集成：直插 users(deleted)+mappings → PostgresSocialAccountRepository::findLinkedUser → AccountUnavailable
- [ ] **[评审 P8] Postgres 集成**：预置同名已删/活跃占位用户 → createLinkedUser → nullopt（DO NOTHING 不收养，直击生产 upsert）
- [ ] 软删用户的 /api/me/profile → 401（token 已吊销）或 404，绝无 200+数据
- [ ] SocialAuthServiceTest/SocialLoginHttpTest 既有用例适配后全过

### C6. #60-4：注释改写
文件：`libs/drogon/src/controllers/TokenEndpointController.cc`（:1926-1929）、`libs/drogon/include/authforge/drogon/authz/ResourceScopeRegistry.h`（:86-88）
**验收**：[ ] 注释陈述"handler 是唯一执行点；registry 刻意排除 userinfo"（评审）

### C7. #60-5：Mapper 卫生（C1-C5 未覆盖的既有违规）
文件：UserAdminService.cc（:459,:474,:706,:1001,:1007,:1056,:1063,:1116,:1134,:1221,:1237,:1270）
**验收**：[ ] 全文件无裸 Mapper 构造（评审，grep 验证）

## Phase 2 — 前端（frontends/admin）

### C8. org_id 清除 + mock 同步
文件：`frontends/admin/src/pages/users/UserDetailPage.vue`、`frontends/admin/tests/e2e/helpers/mock-api.ts`
1. Vue：清空→发送 `org_id: null`；非空非数字前端拦截不发。
2. mock：PUT users/{id} 处理 org_id null → 清除；类型错误 → 400（对齐后端）。
**验收**：
- [ ] e2e：编辑用户清空 org 保存 → 重新打开显示空
- [ ] `npm run build`（tsc）过；既有 16 e2e + 单测全过

## Phase 3 — openapi / 文档

### C9. openapi 同步（用 openapi-update skill 流程）
文件：`apps/server/openapi.yaml`
- PUT body 补全字段（org_id nullable）+400/409；DELETE/disable/roles 补 409；DELETE 200 说明 tokens_revoked；POST 201 说明 roles_assigned/roles_failed。
**验收**：[ ] openapi 与实现字段一致；[ ] SDK 契约文档核对（docs/ 下 SDK 契约提及 admin users API 处）

### C9b. [评审 P1/BLOCKER] C++ SDK 头 api-diff 基线批准
`tools/api-diff/api_diff.py` 守卫的是 `libs/*/include/authforge/**` C++ 头（与 openapi.yaml 无关）。C4/C5 对 `UserAdminService.h`（新增 isLastActiveAdmin 声明，additive）与 `ISocialAccountRepository.h`（findLinkedUser 签名变更，BREAKING）产生漂移。
- 步骤：跑 api_diff.py 确认漂移清单 → `--update-baseline --force` 单独 chore commit（先例：a9a1bf8），commit message 写明理由（接口消费方全部在树内：GitHubAuthService/Fake/Postgres repo/测试；findLinkedUser 非外部 SDK 稳定面）。
**验收**：[ ] api_diff.py 通过新基线；[ ] 批准 commit 独立可评审；版本政策在 C12 一并给出

## Phase 4 — 验证

### C10. 全量验证
1. 后端：`scripts/backend/full_test.bat` 8 步（DB 重置→ORM→构建→ctest→服务器→59 OAuth2→52 Admin→关闭）
2. 前端：admin（build+16 e2e）+ user（build+8 e2e+unit 5 文件）
3. C++ 头 api-diff（C9b 基线后）+ openapi 一致性
**验收**：[ ] 全绿，无跳过的意外失败

## Phase 5 — PR 与收尾

### C11. PR
- 分支推送（SSH remote，记忆：HTTPS 403）
- PR 描述：逐 issue 修复说明 + 评审采纳记录（含 F1 接管向量）+ 行为变更清单（400/409/new response fields/deleteAccount 软删/social 登录拒绝已删/锁定用户）
- 子代理评审 PR；修复评审发现
- 关联 issues：`Fixes #53` `Fixes #56` `Fixes #58` `Fixes #59` `Fixes #60`，#54 部分（执行部分；D1 决策后补）

### C12. 版本号评估
- 检查 CHANGELOG/版本机制（release skill）→ 给出建议（见最终汇报）

## 明确不做（见设计 §7）
D1/D2 决策项、会话语义、搜索性能、V006 漂移、F16 残余面。
