# 实施计划：email 优先的用户认证

**关联方案**：[email-first-auth-design.md](email-first-auth-design.md)
**状态**：待批准
**前置条件**：方案文档第 7 节"待决策问题"已确认

---

## 总览

分 **6 个阶段**，按依赖顺序推进。每阶段独立可验证、可回滚。预计总工作量：2-3 个工作日。

```
阶段0 前置验证
  ↓
阶段1 DB migration（username 可选）
  ↓
阶段2 校验层（RuleSet）
  ↓
阶段3 服务层（AuthService + SessionController）
  ↓
阶段4 前端（登录/注册页）
  ↓
阶段5 测试（新增+兼容）
  ↓
阶段6 集成验证 + 提交
```

**TDD 原则**：阶段 2/3 每个改动先写测试（红），再实现（绿），再重构。

---

## 阶段 0：前置验证

**目的**：确认 migration 无阻塞、baseline 测试通过

| 步骤 | 验证标准 |
|------|---------|
| 0.1 检查存量 username 长度 | `SELECT MAX(length(username)) FROM users` ≤ 100 |
| 0.2 检查存量无 username=NULL | `SELECT COUNT(*) FROM users WHERE username IS NULL` = 0（迁移前） |
| 0.3 跑现有测试 baseline | `ctest -R Unit` 全绿，记录通过数作为对照 |
| 0.4 确认 admin 账号有 email | `SELECT email FROM users WHERE username='admin'` 非空 |

**checkpoint**：若 0.1/0.2 不满足，先处理数据再继续；若 0.4 admin 无 email，补 seed。

---

## 阶段 1：DB migration

**目的**：放宽 username 约束，为可选铺路

### 改动
新建 `OAuth2Server/sql/migrations/V020__username_optional.sql`：

```sql
ALTER TABLE users ALTER COLUMN username DROP NOT NULL;
ALTER TABLE users ALTER COLUMN username TYPE VARCHAR(100);
ALTER TABLE users DROP CONSTRAINT IF EXISTS users_username_check;
ALTER TABLE users ADD CONSTRAINT users_username_check
    CHECK (username IS NULL OR username <> '');
```

### 验证
```bash
# 应用迁移
docker exec -i oauth2-postgres psql -U oauth2_user -d oauth2_db < OAuth2Server/sql/migrations/V020__username_optional.sql

# 验证约束变化
docker exec oauth2-postgres psql -U oauth2_user -d oauth2_db -c "\d users" | grep username
# 预期：username | character varying(100) | （无 NOT NULL）

# 验证 UNIQUE 保留（NULL 豁免）
# 插入两个 NULL username 应成功
```

### ORM 模型同步
按 CLAUDE.md 规则，**用 `./manage.sh generate-models` 重新生成 ORM**（不要手改 Users.cc）。生成后 diff 确认 username 列元数据从 `50,1`（NOT NULL）变为 `100,0`（可空）。

**checkpoint**：migration 应用成功 + ORM 重新生成 + baseline 测试仍全绿。

**风险**：若 `generate-models` 不便运行，参考上次邮箱迁移的做法（同步更新 Users.cc 元数据），但需在 PR 说明。

---

## 阶段 2：校验层（RuleSet）

**目的**：登录/注册校验支持 email 标识

### 2.1 改 `RuleSet::login`

**文件**：`OAuth2Plugin/src/validation/RuleSet.cc:389`

改动点：
- 解析 `username`（标识）和 `email` 两个字段（兼容前端继续用 username 字段）
- 校验：标识（username 或 email）至少一个非空；email 非空时走 `EMAIL_PATTERN`
- 移除"username 必填"硬约束

### 2.2 改 `RuleSet::registerUser`

**文件**：`OAuth2Plugin/src/validation/RuleSet.cc:435`

改动点：
- `email` 改为必填（`if (email.empty()) errors.push_back("email is required")`）
- `username` 改为可选（移除必填校验，保留非空时的长度校验）

### 验证（TDD，先写测试）
新建 `OAuth2Server/test/unit/validation/LoginIdentifierTest.cc`：

```
测试用例：
- login: 仅 username 提交 → 通过
- login: 仅 email 提交 → 通过
- login: email 格式非法 → 拒绝
- login: username 和 email 都空 → 拒绝
- register: 无 email → 拒绝
- register: 无 username（仅 email）→ 通过
```

```bash
./manage.ps1 build-backend -debug
cd build/OAuth2Server/test/Debug && ./OAuth2Test_test.exe -r "Unit_*LoginIdentifier*"
```

**checkpoint**：新测试全绿，baseline 测试不受影响（login 的 username-only 用例仍通过）。

---

## 阶段 3：服务层

**目的**：登录查询支持 email，注册强制 email

### 3.1 改 `AuthService::validateUser`

**文件**：`OAuth2Server/AuthService.cc:16`

改动点：
- 接收"登录标识"参数（语义从 username 扩展）
- 分流逻辑：含 `@` → `normalizeEmail` → `findOne(email)`；否则 `findOne(username)`
- **防枚举**：查不到用户时，返回与"密码错误"相同的错误（核查现状，必要时统一）

### 3.2 改 `SessionController::login`

**文件**：`OAuth2Server/controllers/SessionController.cc:320`

改动点：
- 从请求取 `username` 字段值作为"登录标识"传给 validateUser
- 不改 API 字段名（前端继续提交 username）

### 3.3 `AuthService::registerUser` 兜底

**文件**：`OAuth2Server/AuthService.cc:170`

改动点：
- email 必填兜底校验（RuleSet 已校，这里防绕过）
- username 可选逻辑已是现状（`if (!email.empty())` 同理对 username）

### 验证（TDD）
新建 `OAuth2Server/test/integration/auth/EmailLoginTest.cc`：

```
测试用例：
- username=admin 登录成功（存量兼容）
- email=admin@example.com 登录成功
- Gmail 别名：用 user.x@gmail.com 注册，用 userx@gmail.com 登录成功
- 不存在的 email 登录 → 返回统一错误（防枚举验证）
- 仅 email 注册（无 username）成功
```

**checkpoint**：集成测试全绿，关键验证 Gmail 别名一致性。

---

## 阶段 4：前端

**目的**：登录/注册页支持 email

### 4.1 用户前端
| 文件 | 改动 |
|------|------|
| `OAuth2Frontend/src/pages/auth/LoginPage.vue` | label `Username` → `Email or Username`，placeholder → `you@example.com`，autocomplete 保留 `username` |
| `OAuth2Frontend/src/pages/auth/RegisterPage.vue` | email 标 required，username 标 optional（加"可选"提示） |
| `OAuth2Frontend/src/services/authService.ts` | 函数注释更新为"登录标识"（不改字段名） |

### 4.2 管理后台
| 文件 | 改动 |
|------|------|
| `OAuth2Admin/src/pages/LoginPage.vue` | 同 4.1 登录页改法 |

### 4.3 后端 CSP 登录页
| 文件 | 改动 |
|------|------|
| `OAuth2Server/views/login.csp` | label/placeholder 改为 `Email or Username` |

### 验证
```bash
# 前端启动后手动验证
cd OAuth2Frontend && npm run dev   # localhost:5173
# 浏览器测：用 admin 登录、用 admin@example.com 登录、注册仅填 email
```

**checkpoint**：三种登录方式（username / email / Gmail 别名）前端可走通。

---

## 阶段 5：测试兼容

**目的**：存量测试不破坏，新增覆盖

### 存量测试（保持兼容）
以下测试**不改**，验证存量 username 登录仍工作：
- `test/integration/auth/LoginEnforcementTest.cc`（`username='admin'`）
- `test/e2e/oauth2_flows/FunctionalTest.cc`
- `test/security/injection/DbLeakVerificationTest.cc`
- `scripts/backend/test-oauth2-endpoints.{sh,ps1}`（`username=admin&password=admin`）

### 新增测试
- `LoginIdentifierTest.cc`（阶段 2，单元）
- `EmailLoginTest.cc`（阶段 3，集成）
- 在 `test-oauth2-endpoints.sh/.ps1` 各**新增**（不删旧）：
  - Test: email 登录
  - Test: 仅 email 注册

### 验证
```bash
./manage.ps1 test-backend          # 全套 ctest
./scripts/backend/test-oauth2-endpoints.ps1   # PowerShell 端点测试
bash ./scripts/backend/test-oauth2-endpoints.sh   # Bash 端点测试
```

**checkpoint**：所有测试（存量+新增）全绿。

---

## 阶段 6：集成验证 + 提交

### 6.1 端到端验证（Docker Desktop）
```bash
# 重建后端镜像
docker compose -f deploy/docker/docker-compose.yml up -d --build oauth2-backend

# 验证三种登录
curl -X POST http://localhost:5555/oauth2/login \
  -d "username=admin&password=admin&client_id=vue-client&..."
curl -X POST http://localhost:5555/oauth2/login \
  -d "username=admin@example.com&password=admin&client_id=vue-client& ..."

# 验证仅 email 注册
curl -X POST http://localhost:5555/api/register \
  -d "email=newuser@test.com&password=TestPass123"
```

### 6.2 文档更新
- 更新 [verification-checklist.md](../ops/verification-checklist.md)：登录测试加 email 用例
- 更新 [deployment-windows-docker-desktop.md](../ops/deployment-windows-docker-desktop.md)：管理员账号说明加 email 登录

### 6.3 提交策略
按层分多个提交（便于 review 和回滚）：
```
commit 1: feat(db): make username optional (V020 migration + ORM regen)
commit 2: feat(validation): support email identifier in login/register rules
commit 3: feat(auth): allow login by email or username in AuthService
commit 4: feat(frontend): accept email or username on login/register pages
commit 5: test: add email login and username-optional registration tests
commit 6: docs: update verification checklist for email login
```

**checkpoint**：每个 commit 独立通过测试。`git push` 需人工操作（项目规则）。

---

## 验收标准

实施完成的判定标准：

- [ ] 阶段 0 四项前置检查通过
- [ ] V020 migration 应用成功，username 可空、VARCHAR(100)、UNIQUE 保留
- [ ] ORM 重新生成（generate-models 或同步元数据）
- [ ] `RuleSet::login` 接受 email 标识，`RuleSet::registerUser` 强制 email
- [ ] `AuthService::validateUser` 支持 email/username 分流 + Gmail 归一
- [ ] 前端 4 处（用户登录/注册、admin 登录、CSP）label 改为 `Email or Username`
- [ ] 存量测试全绿（username=admin 登录兼容）
- [ ] 新增测试全绿（email 登录、Gmail 别名、仅 email 注册、防枚举）
- [ ] Docker 端到端：三种登录方式可走通
- [ ] 文档更新
- [ ] 分层提交完成

---

## 时间估算

| 阶段 | 估算 | 说明 |
|------|------|------|
| 0 前置 | 0.5h | 数据检查 + baseline |
| 1 DB | 1h | migration + ORM 重新生成（generate-models 可能慢） |
| 2 校验层 | 2h | TDD，2 个 RuleSet 方法 + 单元测试 |
| 3 服务层 | 3h | TDD，validateUser 分流 + 集成测试 |
| 4 前端 | 1.5h | 4 处改动，主要是验证 |
| 5 测试兼容 | 1h | 跑全套 + 新增端点测试 |
| 6 集成 + 提交 | 1h | Docker 验证 + 文档 + 分层提交 |
| **合计** | **~10h** | 2-3 个工作日 |

---

## 依赖与阻塞

- **阻塞**：方案文档第 7 节的 4 个待决策问题未确认前，不开始阶段 2
- **外部依赖**：`./manage.sh generate-models` 可用（阶段 1 依赖）
- **环境依赖**：Debug 构建可用（Release 有预存 CRT 问题，本次仍用 Debug 验证）
