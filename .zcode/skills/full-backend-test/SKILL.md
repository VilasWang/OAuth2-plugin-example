---
name: full-backend-test
description: 全量后端测试 — full_test.bat/full-test.sh 8 步流水线（DB重置→ORM→构建→ctest 353→服务器→59 OAuth2端点→52 Admin端点→关闭）
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
cd /d/work/development/Repos/cpp/projects/authforge
powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "& { cmd /c 'scripts\backend\full_test.bat -release 2>&1' }"
```

**WSL/Linux**:
```bash
wsl.exe -d Ubuntu -- bash -c 'export PATH="$HOME/.local/bin:$HOME/.conan2/p/b/drogocb21fab5aca32/p/bin:$PATH" && cd ~/projects/authforge && ./scripts/backend/full-test.sh --release 2>&1'
```

## 8 步说明（全部 PASS 才算通过）

| Step | 内容 | 数量 |
|---|---|---|
| 1 | DB 重置（DROP + CREATE + V001-V023 migrations + seeds） | — |
| 2 | ORM 模型重新生成（drogon_ctl） | — |
| 3 | 项目构建（conan + cmake） | — |
| 4 | ctest（OAuth2Tests ~475 用例 + Contract 84 + ScopeDecisionEngine 152 + EndpointTests_OutOfProcess + 单元 + SdkSmoke） | 353 条目 |
| 5 | 服务器启动（独立 authforge-server 进程） | — |
| 6 | OAuth2 端点测试（health/login/token/userinfo/revoke/MFA/WebAuthn/device/social/password） | 59 |
| 7 | Admin 端点测试（dashboard/clients/tokens/users/roles/scopes/orgs/audit） | 52 |
| 8 | 服务器关闭 | — |

## 通过标准

**8/8 步骤全部 PASS。** 任何步骤失败须报告具体 step + 错误信息。
