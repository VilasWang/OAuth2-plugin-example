# 测试策略与执行指南 (Testing Guide)

本文档说明项目的测试分层策略、各测试文件的覆盖范围，以及如何在本地执行全套测试。

---

## 1. 测试前置要求

在运行测试前，请确保以下服务已就绪：

| 服务 | 地址 | 说明 |
|---|---|---|
| **PostgreSQL** | `localhost:5432` | 数据库名: `oauth2_db` / 用户: `oauth2_user` / 密码: `123456` |
| **Redis** | `localhost:6379` | 密码: `123456`（与 `config.json` 一致）|

> [INFO] **快速启动基础设施**：如果你使用 Docker，可以单独启动 postgres 和 redis 容器:
> ```powershell
> docker run -d -p 5432:5432 -e POSTGRES_USER=oauth2_user -e POSTGRES_PASSWORD=123456 -e POSTGRES_DB=oauth2_db postgres:15-alpine
> docker run -d -p 6379:6379 redis:7-alpine redis-server --requirepass 123456
> ```

---

## 2. 测试分层

测试编译为**两类可执行文件**：

1. **按库的 gtest 二进制文件**（Domain 层，纯单元测试，无 DB/无 Drogon）：
   - `libs/common/test/authforge-common-test` — `ConfigManager`、`ErrorCatalog`、`Result`、值对象
   - `libs/common/testing/test/authforge-common-testing-test` — 假实现（`FakeClock`/`FakeCryptoProvider`/`FakeLogger` 等）的确定性验证
   - `libs/oauth2/test/authforge-oauth2-test` — `TokenService`/`AuthorizationService`/`ClientService`/`JwkManager`/`Pkce`/`ScopeDecisionEngine`/`TokenCrypto`
   - `libs/identity/test/authforge-identity-test` — `AuthService`/`MfaService`/`TotpUtils`/`SessionManager`/`WebAuthnService`/社交登录（Google/WeChat/GitHub）
   - 这些用 gtest（非 `DROGON_TEST`），由各 lib 的 `test/CMakeLists.txt` 通过 `gtest_discover_tests` 注册为独立 ctest 条目。

2. **主测试二进制文件 `tests/authforge-tests`**（`DROGON_TEST` 框架，包含所有需要 Drogon/DB 的层级）：

| 层级 | 目录 | 覆盖范围 | 外部依赖 |
|---|---|---|---|
| **Level 1 — 单元测试** | `tests/unit/`（`config/`、`error/`、`utils/`、`validation/`、`plugin/`、`schema/`、`subject/`、`initorder/`） | 纯逻辑：错误信封、密码哈希、PKCE/CryptoUtils、RuleSet 校验、配置加载、OpenAPI 生成 | 无 |
| **Level 2 — 契约测试** | `tests/contract/` | 跨后端（Postgres/Redis/Memory）的仓储契约一致性：`IClientRepository`/`IGrantRepository`/`ITokenRepository`/`IConsentRepository`/`IUserRepository` | Memory 必跑；Postgres/Redis 在 `getPostgresClientOrNull()`/`getRedisClientOrNull()` 返回空时自动 skip |
| **Level 3 — 集成测试** | `tests/integration/`（`auth/`、`token/`、`storage/`、`concurrency/`、`error/`、`plugin/`） | 完整业务流程、并发竞态、错误信封、插件组装 | Postgres / Redis（memory-only 模式 `-DOAUTH2_MEMORY_TESTS_ONLY=ON` 下跑 Memory 子集） |
| **Level 4 — 安全测试** | `tests/security/` | SQL 注入、XSS、命令注入、CORS、Token 安全、速率限制 | Postgres / Redis |
| **Level 5 — E2E/功能** | `tests/e2e-backend/`、`tests/performance/` | OAuth2 完整流程、性能基准 | Postgres + Redis + Drogon App |

> 内存模式：配置 `-DOAUTH2_MEMORY_TESTS_ONLY=ON` 可在**无外部 DB** 时跑完整套件（Postgres/Redis 测试自动 skip）——这是 Windows CI 的做法。

> 安全测试用例数、功能测试用例数与覆盖清单见各目录下的测试文件头注释；本节不再硬编码具体数量（数量随迭代增长，统一以 §7 的实测统计为准）。

### Level 3 — 端到端集成测试

| 测试文件 | 覆盖范围 | 依赖 |
|---|---|---|
| `IntegrationE2ETest.cc` | 模拟完整 OAuth2 授权码流程：HTTP 请求 → 授权 → 登录 → 换 Token → UserInfo 验证 | Postgres + Redis + 运行中的 Drogon App |

### Level 4 — 安全测试 (Security Tests)

| 测试文件 | 覆盖范围 | 测试数量 |
|---|---|---|
| `SecurityTest.cc` | SQL 注入、XSS、命令注入、输入验证、CORS、Token 安全、速率限制、健康检查安全 | 18 个测试用例 |

**安全测试覆盖** (2026-04-21):
- [PASS] 输入验证: SQL 注入、XSS、命令注入、长度限制、空值验证
- [PASS] 认证授权: 无效凭据、速率限制
- [PASS] CORS 配置: 授权源访问、未授权源拒绝
- [PASS] 敏感数据: POST Body 传递、URL 参数后备兼容性
- [PASS] Token 安全: 无效授权码、缺失授权码、无效 Refresh Token
- [PASS] 安全头: 基础安全头、HSTS 配置
- [PASS] 速率限制: 暴力破解防护
- [PASS] 健康检查: 信息泄露检查

### Level 5 — 功能测试 (Functional Tests)

| 测试文件 | 覆盖范围 | 测试数量 |
|---|---|---|
| `FunctionalTest.cc` | OAuth2 完整流程、错误处理、UTF-8/Emoji 字符、健康检查、RBAC、Token 生命周期、输入验证、速率限制 | 21 个测试用例 |

**功能测试覆盖** (2026-04-21):
- [PASS] OAuth2 完整流程: 授权码流程
- [PASS] 错误处理: 5 种错误场景
- [PASS] UTF-8 字符: 中文、Emoji、4-byte UTF-8 序列
- [PASS] 健康检查: 基本检查、字段验证、信息泄露检查
- [PASS] RBAC: 未授权访问、无效 Token
- [PASS] Token 生命周期: 无效授权码、无效 Refresh Token、缺失 Refresh Token
- [PASS] 输入验证: 超长用户名、超长密码
- [PASS] 速率限制: 暴力破解防护检测
- [PASS] 端点可用性: OAuth2 端点响应

**测试通过率**: 18/18 安全测试 (100%) [PASS], 21/21 功能测试 (100%) [PASS]

---

## 3. 执行方式

### 方式一：通过 CTest（推荐）

```powershell
# 在构建完成后执行（目录为 build/<preset>，Windows Release 为 windows-msvc）
cd build\windows-msvc
ctest -C Release --output-on-failure --timeout 120
```

> **输出策略**：默认仅打印失败用例的日志与末尾汇总（`成功数 / 失败数 / 总数`）。如需查看每个用例（含通过用例）的完整输出，加 `--verbose`（`-V`）；如需完全静默、只看汇总行，加 `-Q`。`manage.sh test-backend -q` / `manage.ps1 test-backend -q` 等价于 `-Q`。

### 方式二：直接运行测试可执行文件

测试可执行文件内部会自动启动 Drogon App 实例（`test_main.cc` 中通过信号量同步），**无需手动启动后端服务**。

```powershell
cd build\windows-msvc\tests\Release
.\authforge-tests.exe
```

### 方式三：使用 manage 脚本

`manage.ps1`（Windows）/ `manage.sh`（Linux/macOS）封装了与 CI 相同的构建+测试流程：

```powershell
# 构建并运行后端测试套件（与 manage.sh test-backend 等价）
.\manage.ps1 test-backend

# 完整循环：构建 + 单元/集成测试 + 管理端点 API 测试
.\manage.ps1 full-test

# 仅跑管理端点 / OAuth2 端点的 API 脚本
.\manage.ps1 test-admin-endpoints
.\manage.ps1 test-oauth2-endpoints
```

---

## 4. 测试输出示例

```
All tests passed (N assertions in M tests)
```

如果出现失败，失败的测试名称和断言位置会被打印：
```
In test case SomeTestName
  SomeTestFile.cc:63  FAILED:
    CHECK(c.has_optional())
```

**常见失败原因**：
- Redis 或 PostgreSQL 服务未启动 → 检查服务是否可达
- Redis 密码不匹配 → 检查 `config.json` 中的 `passwd` 字段
- 数据库未初始化 → 执行 `apps/server/migrations/` 目录下的迁移脚本（后端在 `OAUTH2_AUTO_MIGRATE=true` 时也会自动执行）

---

## 5. CI 中的测试

每次 Push 到 `master` 或发起 PR 时，GitHub Actions CI 会自动执行：

1. 启动 Postgres 和 Redis Service Container
2. 初始化数据库 Schema
3. 编译项目
4. 运行 `ctest`

详见 [CI/CD 指南](ci-cd-guide.md)。

---

## 6. 测试报告 (Test Reports)

项目包含完整的测试报告文档，记录所有测试的执行结果和覆盖率。

### 安全测试报告

[DOC] **Security Test Report**（本地文档，已归档）

**测试日期**: 2026-04-21
**测试结果**: 18/18 通过 (100%) [PASS]

报告包含：
- 完整的安全测试用例列表
- SQL 注入、XSS、命令注入等攻击防护验证
- CORS 和安全头配置验证
- Token 安全和撤销机制验证
- 速率限制和 DoS 防护验证
- 安全评分和特性验证
- 生产环境安全评估

### 功能测试报告

[DOC] **Functional Test Report**（本地文档，已归档）

**测试日期**: 2026-04-21
**测试结果**: 21/21 通过 (100%) [PASS]

报告包含：
- 完整的 OAuth2 授权码流程测试
- 错误处理和边缘情况测试
- UTF-8 和 Emoji 字符处理测试（包括 4-byte UTF-8 序列）
- RBAC 权限控制测试
- Token 生命周期管理测试
- 输入验证和 DoS 防护测试
- 健康检查和端点可用性测试
- 性能指标和测试自动化建议

### Bug 状态报告

[DOC] **Remaining Bugs Analysis**（本地文档，已归档）

**生成日期**: 2026-04-21
**总Bug数**: 35 个
**已修复**: 18 个 (51%)
**剩余未修复**: 17 个 (低优先级技术债务)
**已确认为误报**: 1 个 (Bug #16 - DB连接泄漏)

报告包含：
- 详细的 Bug 分类和优先级评估
- 每个 Bug 的修复状态和建议
- 生产环境影响评估
- 剩余 Bug 的风险分析和处理建议
- 生产就绪状态评估：[PASS] **已就绪**

### 数据库连接泄漏验证报告

[DOC] **DB Leak Verification Report**（本地文档，已归档）

**验证日期**: 2026-04-21
**结论**: [PASS] **Bug #16 为误报 (FALSE POSITIVE)**

报告包含：
- Drogon 框架连接池架构分析
- `getDbClient()` 返回类型和生命周期说明
- Lambda 捕获行为和引用计数机制
- 代码模式正确性证明（基于官方文档）
- 测试证据和配置分析
- 详细的连接流图和架构说明

---

## 7. 测试覆盖率总结

### 总体测试状态

> 以下数字为**实测统计**（Windows MSVC Release 构建，无外部 DB，Postgres/Redis 测试 skip）。在带 Postgres+Redis 的环境（如 Linux CI 或本地 WSL+Docker）下，被 skip 的契约/集成测试会激活，ctest 条目数会进一步增加。

| 测试来源 | 通过 | 失败 | 总计 | 通过率 |
|---------|------|------|------|--------|
| **按库 gtest 二进制**（common 40 + common-testing 43 + oauth2 151 + identity 130） | 364 | 0 | 364 | 100% |
| **主测试二进制 ctest 条目**（含 Contract 标签 + OAuth2Tests 全量） | 450 | 0 | 450 | 100% |

> 注：两列数字有重叠关系——主二进制内含所有 `DROGON_TEST` 单元/集成测试（作为一个 `OAuth2Tests` 条目运行）；按库 gtest 二进制是 Domain 层的纯单元测试，独立编译运行。`450` 条 ctest 中 84 条带 `Contract` 标签（可用 `ctest -L Contract` 单独跑）。

### 代码覆盖率

实测行覆盖率（gcov，gcc 13.3 Debug 构建，WSL Ubuntu 24.04，Postgres+Redis 激活；排除 ORM 自动生成的 `models/`）：

| 库 | 行覆盖 | 备注 |
|---|---|---|
| libs/common | 98.8% (686/694) | ErrorCatalog/Result/值对象（由 per-lib gtest 二进制 `authforge-common-test` 驱动） |
| libs/identity | **96.9%** (590/609) | Auth/Mfa/WebAuthn/Social/Totp/Session |
| libs/storage-memory | **97.1%** (431/444) | Memory 后端全方法覆盖（CI 必跑路径） |
| libs/oauth2 | **92.9%** (625/673) | TokenService/AuthService/ClientService/JwkManager/Pkce |
| libs/storage-redis | 46.2% (306/663) | 契约测试覆盖 getClient/validate/grant/token/consent 主路径；Lua 脚本与 transaction CRUD 待补 |
| libs/storage-postgres | 42.5% (705/1657) | 契约测试覆盖主路径；剩余盲区为事务/错误回退分支（需注入故障才能触发） |
| libs/drogon | **49.8%** (4477/8998) | admin 0%→55-69%、admin 控制器 0%→91-100%、authorize/health/discovery/mfa/deviceauth/userselfservice/apidoc 控制器补强；剩余盲区：社交 OAuth 控制器（GitHub 5.9%/WeChat 14.3%/Google 16.3%，需外部 provider mock）、WebAuthn 9.9%（需典礼脚手架） |
| **整体** | **55.2%** (7452/13502) | 本轮从 48.5% 基线提升 +6.7pp（admin 层从 0% 起，是最大单项增量） |

> 上一轮基线为 48.5% (7091/14631)；本轮通过 admin 层 HTTP 集成测试（`tests/integration/admin/*` + `tests/common/HttpTestClient.h` 的 admin 登录 recipe + `tests/common/StorageSeed.h` 的进程内 seeder）将 admin 服务从 0% 提升到 55-69%，admin 控制器从 0% 提升到 91-100%；并补强了 authorize/health/discovery/mfa/deviceauth/userselfservice/apidoc 控制器的可测分支。剩余盲区集中在需要外部 mock 的社交 OAuth 与 WebAuthn 控制器——不引入 mock 框架的现实上限约 55-60%。

#### ⚠️ 实测覆盖率必须跑全部 5 个测试二进制

实测数字依赖**全部 5 个测试二进制**都执行（仅跑主二进制 `authforge-tests` 会漏掉 4 个 per-lib gtest 二进制贡献的 domain 层覆盖率，common 会被低估到 ~60%）：

1. `libs/common/test/authforge-common-test`（40 用例）
2. `libs/common/testing/test/authforge-common-testing-test`（43 用例）
3. `libs/identity/test/authforge-identity-test`（130 用例）
4. `libs/oauth2/test/authforge-oauth2-test`（151 用例）
5. `tests/authforge-tests`（450 条 ctest，含所有 `DROGON_TEST` 单元/集成/契约/admin HTTP 测试）

在 coverage 构建目录下依次运行这 5 个二进制后再聚合 `.gcda`。

#### ⚠️ gcovr 路径匹配 bug —— 用 `scripts/measure_coverage.py` 代替

`gcovr 8.6` 对部分文件（如 `ClientManagementService.cc`）会误报 **0%**：raw `gcov` 明确显示 `Lines executed:55.79% of 328`，但 gcovr 的 `--print-summary` 只列出文件名不带百分比（gcovr 的源路径匹配对含绝对路径 + Drogon 头文件的 `.gcov` 输出处理不一致）。这是 gcovr 的已知路径匹配问题，不是零计数 bug（gcov flush 工作正常，见下）。

**可靠的聚合方式**：`scripts/measure_coverage.py` 直接用 `gcov -j`（JSON 格式，每文件 `{file, lines[{count, unexecuted_block}]}`）聚合，绕过 gcovr 的文本路径匹配。用法：

```bash
cd <repo>
# 先跑全部 5 个二进制（见上一节），再生成 JSON + 聚合：
find build/linux-coverage/libs -path "*/src/*" -name "*.gcda" ! -path "*/models/*" \
  | xargs -I{} bash -c 'cd "$(dirname {})" && gcov -j "$(basename {})" >/dev/null 2>&1'
find build/linux-coverage/libs -path "*/src/*" -name "*.gcov.json.gz" ! -path "*/models/*" \
  | python3 scripts/measure_coverage.py
```

gcovr 仍可用于生成逐文件 HTML 报告（`--html-details`），但**汇总百分比以 `scripts/measure_coverage.py` 为准**。

#### 覆盖率工具链

- `cmake/Coverage.cmake`（`oauth2_apply_gcov(target)`）给每个 first-party 库 + 测试可执行文件加 `-fprofile-arcs -ftest-coverage` 并链接 libgcov。
- `tests/test_main.cc` 在两处 `std::_Exit()` 前显式调用 `__gcov_dump()`：因为 `_Exit` 绕过 `atexit`，libgcov 的计数器 flush 不会自动执行（否则 gcov 读到全 0 计数）。这是 Drogon 测试框架 fast-exit 与 gcov 的已知交互，需在测试 main 里手动补 flush。（注：4 个 per-lib gtest 二进制正常退出，不走 `_Exit`，因此它们不需要手动 flush。）
- 覆盖率当前阶段性目标：60%（本轮 52.9%）。剩余盲区集中在难以 HTTP 测试的控制器：社交 OAuth（GitHub/WeChat/Google，需外部 provider mock）、WebAuthn 典礼、UserSelfService（需逆向 auth 前置 filter）。ORM 自动生成的 `libs/storage-postgres/src/models/*.cc` 已从分母排除。

---

---

## 8. 手动验证与 API 测试 (Manual Validation)

除了自动化测试套件外，项目还提供了用于手动验证端点功能的工具和脚本。

### 8.1 PowerShell 自动化验证脚本
项目提供了完整的 OAuth2 端点测试脚本：`scripts/backend/test-oauth2-endpoints.ps1`。

**使用方法：**
```powershell
# 临时绕过执行策略运行测试
powershell -ExecutionPolicy Bypass -File scripts/backend/test-oauth2-endpoints.ps1
```
该脚本会依次执行健康检查、登录、授权码交换、UserInfo 访问及管理员面板验证。

### 8.2 多环境 API 测试 (curl)
不同的命令行工具对 curl 语法的支持不同：

*   **PowerShell (推荐)**: 使用 `Invoke-RestMethod`。
*   **Git Bash**: 支持标准的 Unix 单引号语法。
*   **CMD**: 需要使用双引号并转义 `&` 符号为 `^&`。

**示例：登录并获取 JSON 响应**
```bash
# Git Bash 示例
curl -X POST http://127.0.0.1:5555/oauth2/login \
  -d 'username=admin&password=admin&client_id=vue-client&redirect_uri=http://localhost:5173/callback&json=true'
```

---

## 9. 故障排查 (Troubleshooting)

### 9.1 常见问题与对策
*   **服务器无法启动**：检查端口 5555 是否被占用 (`netstat -ano | findstr :5555`)，并确保 `config.json` 路径正确。
*   **登录失败 (400)**：确认用户名密码匹配，且数据库中存在该用户。检查 `redirect_uri` 是否与配置完全一致。
*   **Token 交换失败**：授权码 (Code) 仅能使用一次且有有效期。确保 `client_id` 和 `client_secret` 正确。
*   **PowerShell 脚本限制**：如提示“禁止运行脚本”，请使用 `-ExecutionPolicy Bypass` 参数。

### 9.2 调试技巧
*   **日志级别**：排错时可在 `config.json` 中临时将 `log_level` 调为 `DEBUG`（甚至 `TRACE`）以获取详细输出，定位完成后调回 `INFO`。完整的六级语义与约定见 [observability.md §3.2](./observability.md)。
*   **实时日志**：使用 `Get-Content apps/server/logs/drogon.log -Wait -Tail 20` 监控运行状态。

---

**相关文档**:
- [Security Hardening Guide](./security-hardening.md) - 安全加固措施
- [Security Architecture](./security-architecture.md) - 安全架构设计
- [Data Consistency](./data-consistency.md) - 数据一致性和威胁模型
- [API Reference](./api-reference.md) - API 接口文档
- [Bug Analysis] - 完整 Bug 分析报告（本地文档，已归档）
