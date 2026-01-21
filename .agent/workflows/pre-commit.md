---
description: 提交前完整质量检查（Code Review + ORM Gen + Build + Start + Test + Docs）
---

# Pre-Commit 检查

确保代码质量符合提交标准的完整检查流程。

## 1. 代码审查

```powershell
python d:\work\development\Repos\backend\drogon-plugin\OAuth2-plugin-example\.agent\skills\code-review\scripts\run.py --all --fix
```

> 自动修复格式问题，检查代码风格和架构合规性

---

## 2. ORM 模型生成

> 重新生成 Drogon ORM 模型，确保与数据库结构一致。

```powershell
cd d:\work\development\Repos\backend\drogon-plugin\OAuth2-plugin-example\OAuth2Backend\models
drogon_ctl create model .
```

---

## 3. 构建验证

// turbo

```powershell
cd d:\work\development\Repos\backend\drogon-plugin\OAuth2-plugin-example\OAuth2Backend
taskkill /F /IM OAuth2Server.exe 2>$null
.\scripts\build.bat -release
```

---

## 4. 启动验证 (Start & Check & Stop)

// turbo

```powershell
cd d:\work\development\Repos\backend\drogon-plugin\OAuth2-plugin-example\OAuth2Backend\build\Release

# 启动服务
Start-Process -FilePath ".\OAuth2Server.exe" -PassThru

# 等待启动并验证端口
Write-Host "Waiting for server start..."
Start-Sleep -Seconds 3
Test-NetConnection -ComputerName localhost -Port 5555

# 停止服务（避免与测试端口冲突）
taskkill /F /IM OAuth2Server.exe 2>$null
Write-Host "Server verified and stopped."
```

---

## 5. 执行测试

> **注意**：测试运行器会自行启动 Drogon App 实例，因此无需外部服务运行。

// turbo

```powershell
cd d:\work\development\Repos\backend\drogon-plugin\OAuth2-plugin-example\OAuth2Backend\build\test\Release
.\OAuth2Test_test.exe
```

---

## 6. 文档与 README 更新

> **关键步骤**：确保文档与代码保持同步。

**请执行以下检查：**

1. 检查 `docs/` 目录，更新相关技术文档，新功能模块按需新增技术文档。
2. 更新根目录 `README.md`，记录版本变更或新功能。
3. 更新 `task.md` 和 `walkthrough.md` 等 Status Artifacts。

---

## 检查清单

- [ ] Code Review 无错误
- [ ] ORM 模型已更新
- [ ] 构建成功
- [ ] 服务启动验证通过
- [ ] 所有测试通过
- [ ] 文档已更新
- [ ] 可以执行 `git commit`

## 失败处理

遇到问题时：

1. 分析错误原因
2. 修复问题
3. 重新执行失败的步骤
4. 直到全部通过
