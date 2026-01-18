---
description: 测试前置检查清单与测试执行流程
---

# OAuth2Backend 测试流程

## 一、测试前置检查清单

### 1. 配置文件检查

// turbo

```powershell
$configPath = "d:\work\development\Repos\backend\drogon-plugin\OAuth2-plugin-example\OAuth2Backend\build\Release\config.json"
if (Test-Path $configPath) { Write-Host "✅ config.json 存在" } else { Write-Host "❌ config.json 缺失" }
```

### 2. 数据库服务检查（PostgreSQL）

```powershell
# 检查 PostgreSQL 是否运行
Get-Service -Name "postgresql*" | Select-Object Name, Status
```

### 3. Redis 服务检查

```powershell
# 检查 Redis 是否可达
redis-cli ping
```

### 4. 数据库初始化检查

确认已执行 `init_oauth2.sql` 初始化 OAuth2 所需表结构：

```powershell
# 如未初始化，执行：
psql -U postgres -d oauth2_test -f "d:\work\development\Repos\backend\drogon-plugin\OAuth2-plugin-example\OAuth2Backend\init_oauth2.sql"
```

---

## 二、单元测试

### 执行单元测试

// turbo

```powershell
cd d:\work\development\Repos\backend\drogon-plugin\OAuth2-plugin-example\OAuth2Backend\build
ctest --output-on-failure -C Release
```

### 测试覆盖范围

- `ConfigTest` - 配置加载测试
- `MemoryStorageTest` - 内存存储测试
- `RedisStorageTest` - Redis 存储测试
- `PostgresStorageTest` - PostgreSQL 存储测试
- `PluginTest` - OAuth2 插件核心功能测试
- `AdvancedStorageTest` - 高级存储功能测试

---

## 三、集成测试

### 1. 启动后端服务

```powershell
cd d:\work\development\Repos\backend\drogon-plugin\OAuth2-plugin-example\OAuth2Backend\build\Release
Start-Process -FilePath ".\OAuth2Server.exe" -PassThru
```

### 2. API 接口测试

**健康检查：**
// turbo

```powershell
Invoke-RestMethod -Uri "http://localhost:5555/health" -Method GET
```

**Token 端点测试：**

```powershell
$body = @{
    grant_type = "client_credentials"
    client_id = "test_client"
    client_secret = "test_secret"
}
Invoke-RestMethod -Uri "http://localhost:5555/oauth2/token" -Method POST -Body $body
```

---

## 四、浏览器集成测试（Feature 完成时）

> ⚠️ **仅在大型 Feature 开发完成后执行**

### OAuth2 完整流程验证

1. **启动后端服务** (端口 5555)
2. **启动前端服务**：

   ```powershell
   cd d:\work\development\Repos\backend\drogon-plugin\OAuth2-plugin-example\OAuth2Frontend
   npm run dev
   ```

3. **浏览器测试流程**：
   - 访问 <http://localhost:5173>
   - 点击 "Login with Drogon"
   - 使用凭据 `admin` / `admin` 登录
   - 验证重定向和用户信息显示
   - 验证 Token 刷新流程
   - 验证登出功能

### 浏览器测试检查点

- [ ] 登录页面正常加载
- [ ] 授权页面显示正确的 client 信息
- [ ] 授权后正确重定向到 callback
- [ ] Access Token 正确获取并存储
- [ ] UserInfo 接口返回正确用户信息
- [ ] Token 过期后刷新机制正常
- [ ] 登出后 Token 失效

---

## 五、测试结果处理

### 测试通过

- 更新相关文档
- 执行 git commit

### 测试失败

- 分析失败原因
- 修复问题后重新执行测试
- 如重复失败超过 3 次，停止并分析根本原因
