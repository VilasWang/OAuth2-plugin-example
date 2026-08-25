---
name: full-backend-test
description: 全量后端测试 — full_test.bat/full-test.sh 8 步流水线（DB重置→ORM→构建→ctest→服务器→59 OAuth2端点测试→52 Admin端点测试→关闭）
---

# 全量后端测试

执行 `full_test.bat`（Windows）或 `full-test.sh`（WSL/Linux）的完整 8 步流水线。这是后端唯一的全量验证——不可跳过任何步骤。

## 前置检查

1. **PostgreSQL + Redis 运行中**（Windows: 5432/6379；WSL: 同上）
2. **`.wslconfig` 设 `localhostForwarding=false`**（否则 WSL/Windows PG 端口冲突）
3. **WSL 额外**：`jq` + `drogon_ctl` 在 PATH 中

## 执行命令

**Windows**（当前 Shell 是 Git Bash 时）:
```bash
cd "$(git rev-parse --show-toplevel)"
powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "& { cmd /c 'scripts\backend\full_test.bat -release 2>&1' }"
```

**WSL/Linux**:
```bash
wsl.exe -d Ubuntu -- bash -c 'cd /path/to/fulla && ./scripts/backend/full-test.sh --release 2>&1'
```

## 8 步说明（全部 PASS 才算通过）

| Step | 内容 | 数量 |
|---|---|---|
| 1 | DB 重置（DROP + CREATE + V001–V024 migrations + seeds） | — |
| 2 | ORM 模型重新生成（drogon_ctl） | — |
| 3 | 项目构建（conan + cmake） | — |
| 4 | ctest（后端 `tests/` 子树 86 条：OAuth2Tests 1 条运行约 558 个 DROGON_TEST 用例[含 84 个 Contract]、`Contract.*` 84 条、EndpointTests_OutOfProcess 1 条；外加 SdkSmoke.FullStack 1 条与 libs 的 gtest 库单测约 369 条，全仓约 450+ 条） | 86（仅 tests/ 子树） |
| 5 | 服务器启动（独立 fulla-server 进程） | — |
| 6 | OAuth2 端点测试（health/login/token/userinfo/revoke/MFA/WebAuthn/device/social/password） | 59 |
| 7 | Admin 端点测试（dashboard/clients/tokens/users/roles/scopes/orgs/audit） | 52 |
| 8 | 服务器关闭 | — |

> **关于计数口径（避免混淆）**：`ScopeDecisionEngine` **不是**独立的 ctest 套件——`libs/oauth2/test` 整个 gtest 二进制约有 152 个 `TEST`（`ScopeDecisionEngineTest.cc` 本身仅 17 个），它是独立二进制 `fulla-oauth2-test`，不属于后端 `fulla-tests`。所谓「59 / 52」是 `test-oauth2-endpoints` / `test-admin-endpoints` 脚本里的**测试函数/断言数量**（OAuth2 脚本实际命中约 30 个不同端点，Admin 约 15–20 个资源组），并非独立端点数。ctest 必须从 `build/<preset>` 根目录运行（不是 `build/apps/server` 或 `build/tests`）。

## 通过标准

**8/8 步骤全部 PASS。** 任何步骤失败须报告具体 step + 错误信息。
