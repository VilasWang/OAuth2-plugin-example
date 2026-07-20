---
name: e2e-test
description: 执行OAuth2系统的完整端到端(E2E)测试，验证用户登录、授权码流程、token交换、RBAC权限控制和受保护API访问的完整功能。当用户需要在代码变更后验证整个OAuth2认证流程、测试用户登录和权限管理、验证修复后的安全功能、或在生产部署前进行完整的功能验证时使用此技能。
---

# OAuth2端到端测试

执行完整的OAuth2授权码流程和RBAC权限控制测试，验证系统从前端登录到后端API保护的完整认证链路。

## 使用时机

- 代码变更后验证OAuth2核心流程完整性
- 测试用户登录、权限验证和API访问控制
- 验证安全漏洞修复后的功能正常性
- 生产部署前的完整功能验证
- 回归测试确保修改未破坏现有功能

## 测试范围

### 1. 环境准备
- 数据库重置和schema初始化
- OAuth2测试数据准备
- 服务编译和启动

### 2. 完整OAuth2流程测试
- 用户登录获取授权码
- 授权码交换access token和refresh token
- Token验证和角色识别
- 受保护API资源访问

### 3. RBAC权限验证
- Admin用户权限验证
- 角色识别和权限检查
- 受保护资源访问控制

### 4. 安全功能验证
- 敏感信息POST body传递
- Refresh token撤销机制
- CORS策略验证
- Token过期处理

## 测试流程

### 步骤1: 环境准备

```powershell
# 自动选择最佳环境
$env:OAUTH2_ENV_MODE = "auto"

# 检查 Docker 是否可用
docker ps | Out-Null
if ($?) {
    Write-Host "🐳 Docker 检测到，使用 Docker 模式"
    $env:OAUTH2_ENV_MODE = "docker"
} else {
    Write-Host "💻 使用本地模式"
    $env:OAUTH2_ENV_MODE = "local"
}

# Docker 模式推荐流程
if ($env:OAUTH2_ENV_MODE -eq "docker") {
    Write-Host "使用 Docker 完整测试流程..."
    # 跳到步骤 2 的 Docker 模式
} else {
    Write-Host "使用本地测试流程..."
    # 继续传统本地流程
}
```

### Docker 模式（推荐）

```powershell
# 使用 Docker 专项脚本
scripts/backend/full_test_docker.bat

# 此脚本会自动完成所有测试步骤
```

### 本地模式

```bash
# 重置数据库
cd /path/to/project
$env:PGPASSWORD='123456'
psql -U oauth2_user -d postgres -c "DROP DATABASE IF EXISTS oauth2_db;"
psql -U oauth2_user -d postgres -c "CREATE DATABASE oauth2_db;"
for f in OAuth2Server/sql/migrations/V*.sql; do
    psql -U oauth2_user -d oauth2_db -f "$f"
done
for f in OAuth2Server/sql/seed/*.sql; do
    psql -U oauth2_user -d oauth2_db -f "$f"
done

# 编译服务（如果需要）
.\manage.ps1 build-backend -release
```

### 步骤2: 启动服务

```powershell
# 使用统一管理接口编译
.\manage.ps1 build-backend -release

# 停止旧服务
taskkill /F /IM OAuth2Server.exe 2>$null

# 启动服务（新的构建路径）
$serverPath = "build/OAuth2Server/Release/OAuth2Server.exe"
if (Test-Path $serverPath) {
    Start-Process -FilePath $serverPath -WindowStyle Hidden
    Write-Host "✅ Server started from: $serverPath"
} else {
    Write-Host "❌ Server executable not found at: $serverPath"
    exit 1
}

# 等待服务启动
Start-Sleep -Seconds 3

# 验证服务状态
$response = Invoke-WebRequest -Uri "http://localhost:5555/health" -UseBasicParsing -TimeoutSec 5
if ($response.StatusCode -eq 200) {
    Write-Host "✅ Server is responding"
} else {
    Write-Host "❌ Server health check failed"
    exit 1
}
```

### Docker 模式测试流程

```powershell
# 完整 Docker 测试（推荐）
scripts/backend/full_test_docker.bat

# 此脚本包含：
# 1. PostgreSQL 容器启动
# 2. 数据库初始化
# 3. ORM 模型生成  
# 4. 项目构建
# 5. 单元测试
# 6. 服务启动
# 7. OAuth2 端点测试
# 8. 清理和停止

# 手动 Docker 测试流程
.\manage.ps1 docker-up
Start-Sleep -Seconds 10

# 在 Docker 容器中运行测试
docker exec oauth2-backend /bin/bash -c "cd build && ctest --output-on-failure"

# OAuth2 端点测试
# ... (后续步骤)

# 清理
.\manage.ps1 docker-down
```

### 步骤3: 执行E2E测试

#### 3.1 登录并获取授权码
```bash
curl -s -X POST "http://localhost:5555/oauth2/login" \
  -H "Content-Type: application/x-www-form-urlencoded" \
  -d "username=admin&password=admin&client_id=vue-client&redirect_uri=http://localhost:5173/callback&scope=openid&state=e2e_test"
```

**预期结果**: HTTP 302重定向，包含授权码

#### 3.2 交换Token
```bash
# 提取授权码并交换token
CODE="<从上一步响应中提取的授权码>"

curl -s -X POST "http://localhost:5555/oauth2/token" \
  -H "Content-Type: application/x-www-form-urlencoded" \
  -d "grant_type=authorization_code&code=${CODE}&client_id=vue-client&redirect_uri=http://localhost:5173/callback"
```

**预期结果**: 返回access_token, refresh_token, roles数组

#### 3.3 访问受保护的Admin资源
```bash
# 使用access_token访问admin dashboard
ACCESS_TOKEN="<从上一步响应中提取的access_token>"

curl -s -X GET "http://localhost:5555/api/admin/dashboard" \
  -H "Authorization: Bearer ${ACCESS_TOKEN}"
```

**预期结果**: 返回admin dashboard数据

### 步骤4: 清理环境
```bash
# 停止服务
taskkill /F /IM OAuth2Server.exe 2>$null
```

## 测试验证标准

### ✅ 成功标准
- 登录返回HTTP 302重定向包含授权码
- Token交换返回包含access_token和roles的JSON
- Admin API返回200状态码和正确的响应数据
- roles数组包含"admin"角色
- 无服务崩溃或异常退出

### ❌ 失败标准
- 登录失败返回错误状态码
- Token交换失败或缺少必需字段
- API访问返回401/403权限错误
- 服务崩溃或无响应
- roles不包含预期的角色

## 错误处理

### 常见问题排查

#### 服务启动失败
```bash
# 检查端口占用
netstat -ano | findstr :5555

# 检查服务日志
cd build/OAuth2Server/Release
.\OAuth2Server.exe
```

#### 数据库连接失败
```bash
# 验证数据库连接
psql -U oauth2_user -d oauth2_db -c "SELECT 1;"

# 检查客户端数据
psql -U oauth2_user -d oauth2_db -c "SELECT * FROM oauth2_clients;"
```

#### 登录失败
```bash
# 验证用户存在
psql -U oauth2_user -d oauth2_db -c "SELECT * FROM users WHERE username='admin';"

# 验证客户端配置
psql -U oauth2_user -d oauth2_db -c "SELECT * FROM oauth2_clients WHERE client_id='vue-client';"
```

## 性能指标

- **服务启动时间**: < 5 秒
- **登录响应时间**: < 500ms  
- **Token 交换时间**: < 300ms
- **API 访问时间**: < 200ms
- **完整流程时间**: < 2 秒
- **Docker 模式完整流程**: < 5 分钟（包含环境准备）

## 自动化脚本

### PowerShell脚本版本
```powershell
# 完整E2E测试脚本
$ErrorActionPreference = "Stop"

# 1. 启动服务
Write-Host "Starting OAuth2Server..."
cd build/OAuth2Server/Release
Start-Process -FilePath ".\OAuth2Server.exe" -WindowStyle Hidden
Start-Sleep -Seconds 3

# 2. 登录
Write-Host "Testing Login..."
$LoginResp = curl -s -X POST "http://localhost:5555/oauth2/login" `
  -H "Content-Type: application/x-www-form-urlencoded" `
  -d "username=admin&password=admin&client_id=vue-client&redirect_uri=http://localhost:5173/callback&scope=openid&state=e2e_test"

if ($LoginResp -match "code=([a-f0-9\-]+)") {
    $Code = $matches[1]
    Write-Host "✅ Login successful, code: $Code"
    
    # 3. 交换Token
    Write-Host "Exchanging token..."
    $TokenResp = curl -s -X POST "http://localhost:5555/oauth2/token" `
      -H "Content-Type: application/x-www-form-urlencoded" `
      -d "grant_type=authorization_code&code=$Code&client_id=vue-client&redirect_uri=http://localhost:5173/callback" | ConvertFrom-Json
    
    if ($TokenResp.access_token) {
        Write-Host "✅ Token obtained: $($TokenResp.access_token)"
        
        # 4. 访问Admin API
        Write-Host "Testing admin API access..."
        $ApiResp = curl -s -X GET "http://localhost:5555/api/admin/dashboard" `
          -H "Authorization: Bearer $($TokenResp.access_token)"
        
        if ($ApiResp -match "success") {
            Write-Host "✅ E2E test completed successfully!"
        } else {
            Write-Error "❌ API access failed"
        }
    } else {
        Write-Error "❌ Token exchange failed"
    }
} else {
    Write-Error "❌ Login failed"
}

# 5. 清理
Write-Host "Stopping service..."
taskkill /F /IM OAuth2Server.exe 2>$null
```

## 集成建议

### CI/CD 集成
```yaml
# .github/workflows/e2e-test.yml
name: E2E Tests
on: [push, pull_request]

jobs:
  e2e:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v3
      
      - name: Start Docker services
        run: docker-compose up -d
        
      - name: Run E2E tests
        run: scripts/backend/full_test_docker.bat
        
      - name: Stop Docker services  
        run: docker-compose down
```

### 监控集成
- 将测试结果发送到监控系统
- 记录关键性能指标（登录时间、token 交换时间等）
- 设置告警阈值（响应时间 > 1秒触发告警）

### 定期执行
- 每次部署后立即执行 E2E 测试
- 每日自动执行作为健康检查
- 定期回归测试确保稳定性
