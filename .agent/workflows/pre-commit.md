---
description: 提交前完整质量检查（Code Review + Build + Start + Test）
---

# Pre-Commit 检查

确保代码质量符合提交标准的完整检查流程。

## 1. 代码审查

```powershell
python d:\work\development\Repos\backend\drogon-plugin\OAuth2-plugin-example\.agent\skills\code-review\scripts\run.py --all --fix
```

> 自动修复格式问题，检查代码风格和架构合规性

---

## 2. 构建验证

// turbo

```powershell
cd d:\work\development\Repos\backend\drogon-plugin\OAuth2-plugin-example\OAuth2Backend
taskkill /F /IM OAuth2Server.exe 2>$null
.\scripts\build.bat -release
```

---

## 3. 启动验证 (Start & Check & Stop)

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

## 4. 执行测试

> **注意**：测试运行器会自行启动 Drogon App 实例，因此无需外部服务运行。

// turbo

```powershell
cd d:\work\development\Repos\backend\drogon-plugin\OAuth2-plugin-example\OAuth2Backend\build\test\Release
.\OAuth2Test_test.exe
```

---

## 检查清单

- [ ] Code Review 无错误
- [ ] 构建成功
- [ ] 所有测试通过
- [ ] 可以执行 `git commit`

## 失败处理

遇到问题时：

1. 分析错误原因
2. 修复问题
3. 重新执行失败的步骤
4. 直到全部通过
