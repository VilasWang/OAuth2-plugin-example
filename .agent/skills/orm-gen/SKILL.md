---
name: orm-gen
description: 重新生成 Drogon ORM 模型类（基于当前数据库表结构）
disable-model-invocation: true
---

# ORM 模型生成技能

重新生成 Drogon ORM 模型类以匹配最新的数据库表结构。

## 使用方法

通过用户调用：`/orm-gen`

## 前置条件检查

```bash
# 1. 检查 PostgreSQL 服务是否运行
pg_isready -h localhost -p 5432 || echo "❌ PostgreSQL not running"

# 2. 检查数据库是否存在
export PGPASSWORD='123456'
psql -h localhost -U oauth2_user -d oauth2_db -c "SELECT 1;" || echo "❌ Database oauth2_db not found"

# 3. 检查 drogon_ctl 工具是否安装
which drogon_ctl || echo "❌ drogon_ctl not found"
drogon_ctl version || echo "❌ drogon_ctl not working"

# 4. 检查 models 目录是否存在
ls OAuth2Server/model.json || echo "❌ model.json not found"
```

## 完整工作流程

### 1. 确认数据库表结构

```bash
# 查看当前数据库中的所有表
export PGPASSWORD='123456'
psql -h localhost -U oauth2_user -d oauth2_db -c "\dt"

# 预期输出应包含：
# - oauth2_clients
# - oauth2_codes
# - oauth2_access_tokens
# - oauth2_refresh_tokens
# - users
# - roles
# - permissions
# - user_roles
# - role_permissions
```

### 2. 检查 model.json 配置

```bash
# 查看当前 ORM 生成配置
cat OAuth2Server/model.json
```

**标准配置**:
```json
{
    "rdbms": "postgresql",
    "host": "127.0.0.1",
    "port": 5432,
    "dbname": "oauth2_db",
    "user": "test",
    "passwd": "123456",
    "tables": [
        "users",
        "roles",
        "permissions",
        "user_roles",
        "role_permissions",
        "oauth2_clients",
        "oauth2_codes",
        "oauth2_access_tokens",
        "oauth2_refresh_tokens"
    ]
}
```

### 3. 备份现有模型文件（可选但推荐）

```powershell
# Windows PowerShell
cd OAuth2Server
$timestamp = Get-Date -Format "yyyyMMdd_HHmmss"
$backupDir = "models_backup_$timestamp"
New-Item -ItemType Directory -Path $backupDir | Out-Null

# 备份模型文件（如果存在）
if (Test-Path "model.json") {
    Copy-Item model.json $backupDir\
}
if (Test-Path "models") {
    Copy-Item models\*.h, models\*.cc $backupDir\ 2>$null
}
Write-Host "✅ Models backed up to $backupDir"
```

```bash
# Linux/macOS  
cd OAuth2Server
timestamp=$(date +%Y%m%d_%H%M%S)
backup_dir="models_backup_$timestamp"
mkdir -p $backup_dir

# 备份模型文件
cp model.json $backup_dir/ 2>/dev/null || true
cp models/*.h models/*.cc $backup_dir/ 2>/dev/null || true
echo "✅ Models backed up to $backup_dir"
```

### 4. 准备 ORM 生成目录

```powershell
# Windows PowerShell
# 检查 OAuth2Server 目录结构
cd d:\work\development\Repos\backend\drogon-plugin\authforge\OAuth2Server

# 创建 models 目录（如果不存在）
if (!(Test-Path "models")) {
    New-Item -ItemType Directory -Path "models" | Out-Null
    Write-Host "✅ Created models directory"
}

# 进入 models 目录
cd models
```

```bash
# Linux/macOS
cd /path/to/authforge/OAuth2Server

# 创建 models 目录（如果不存在）
mkdir -p models
echo "✅ Created models directory"

# 进入 models 目录
cd models
```

### 5. 删除旧的模型文件

```powershell
# Windows PowerShell
Get-ChildItem -Filter "*.h" | Where-Object { $_.Name -ne "orm_compat.h" } | Remove-Item
Get-ChildItem -Filter "*.cc" | Remove-Item
Write-Host "✅ Old model files removed"
```

```bash
# Linux/macOS
rm -f *.h *.cc
echo "✅ Old model files removed"
```

### 6. 使用脚本生成（推荐）

```powershell
# Windows PowerShell - 使用专项脚本
cd d:\work\development\Repos\backend\drogon-plugin\authforge
scripts/backend/generate_models.bat -y

# 此脚本会自动完成：
# 1. 检查数据库连接
# 2. 验证 model.json 配置
# 3. 备份现有模型文件
# 4. 执行 drogon_ctl 生成
# 5. 验证生成结果
# 6. 返回到项目根目录
```

### 7. 手动生成 ORM 模型

```bash
# 确保在正确的目录
cd OAuth2Server/models

# 执行 drogon_ctl 生成命令
drogon_ctl create model ../
```

**预期输出**:
```
Generating models for tables:
  - users
  - roles
  - permissions
  - user_roles
  - role_permissions
  - oauth2_clients
  - oauth2_codes
  - oauth2_access_tokens
  - oauth2_refresh_tokens

Models generated successfully!
```

### 8. 验证生成结果

```powershell
# Windows PowerShell
Write-Host "`n=== Generated Files ==="
Get-ChildItem *.h, *.cc | Select-Object Name, LastWriteTime | Format-Table -AutoSize

# 验证关键文件
$requiredFiles = @(
    "Users.h", "Users.cc",
    "Roles.h", "Roles.cc",
    "Permissions.h", "Permissions.cc",
    "UserRoles.h", "UserRoles.cc",
    "RolePermissions.h", "RolePermissions.cc",
    "Oauth2Clients.h", "Oauth2Clients.cc",
    "Oauth2Codes.h", "Oauth2Codes.cc",
    "Oauth2AccessTokens.h", "Oauth2AccessTokens.cc",
    "Oauth2RefreshTokens.h", "Oauth2RefreshTokens.cc"
)

$missingFiles = @()
foreach ($file in $requiredFiles) {
    if (!(Test-Path $file)) {
        $missingFiles += $file
    }
}

if ($missingFiles.Count -gt 0) {
    Write-Host "❌ Missing files: $($missingFiles -join ', ')" -ForegroundColor Red
    exit 1
} else {
    Write-Host "✅ All required model files generated" -ForegroundColor Green
}
```

```bash
# Linux/macOS
echo "=== Generated Files ==="
ls -lh *.h *.cc | awk '{print $9, $6, $7, $8}'

# 验证关键文件
required_files=(
    "Users.h" "Users.cc"
    "Roles.h" "Roles.cc"
    "Permissions.h" "Permissions.cc"
    "UserRoles.h" "UserRoles.cc"
    "RolePermissions.h" "RolePermissions.cc"
    "Oauth2Clients.h" "Oauth2Clients.cc"
    "Oauth2Codes.h" "Oauth2Codes.cc"
    "Oauth2AccessTokens.h" "Oauth2AccessTokens.cc"
    "Oauth2RefreshTokens.h" "Oauth2RefreshTokens.cc"
)

missing_files=()
for file in "${required_files[@]}"; do
    if [ ! -f "$file" ]; then
        missing_files+=("$file")
    fi
done

if [ ${#missing_files[@]} -gt 0 ]; then
    echo "❌ Missing files: ${missing_files[@]}"
    exit 1
else
    echo "✅ All required model files generated"
fi
```

### 8. 检查生成文件内容

```bash
# 随机抽查一个文件，确认生成正确
head -20 Oauth2Clients.h

# 应该包含：
# - 自动生成的注释："DO NOT EDIT. This file is generated by drogon_ctl"
# - 类定义继承 Drogon ORM 基类
# - Getter/Setter 方法
```

### 9. 重新编译项目

```powershell
# Windows PowerShell - 新的构建路径
cd build/OAuth2Server
cmake --build . --parallel --config Release
if ($LASTEXITCODE -eq 0) {
    Write-Host "✅ Build successful" -ForegroundColor Green
} else {
    Write-Host "❌ Build failed" -ForegroundColor Red
    exit 1
}
```

```bash
# Linux/macOS
cd build/OAuth2Server
cmake --build . --parallel
if [ $? -eq 0 ]; then
    echo "✅ Build successful"
else
    echo "❌ Build failed"
    exit 1
fi
```

### 10. 运行测试验证

```bash
# 运行测试确保 ORM 模型工作正常
cd build
ctest --output-on-failure -C Release
```

## 数据库凭据

| 配置项 | 值 | 说明 |
|-------|-----|------|
| 主机 | 127.0.0.1 / localhost | 本地连接 |
| 端口 | 5432 | PostgreSQL 默认端口 |
| 数据库 | oauth2_db | OAuth2 测试数据库 |
| 用户名 | test | 测试用户 |
| 密码 | 123456 | 测试密码 |

## 生成的模型文件

| 表名 | 头文件 | 源文件 | 说明 |
|------|--------|--------|------|
| users | Users.h | Users.cc | 用户账号表 |
| roles | Roles.h | Roles.cc | 角色表 |
| permissions | Permissions.h | Permissions.cc | 权限表 |
| user_roles | UserRoles.h | UserRoles.cc | 用户-角色关联表 |
| role_permissions | RolePermissions.h | RolePermissions.cc | 角色-权限关联表 |
| oauth2_clients | Oauth2Clients.h | Oauth2Clients.cc | OAuth2 客户端表 |
| oauth2_codes | Oauth2Codes.h | Oauth2Codes.cc | OAuth2 授权码表 |
| oauth2_access_tokens | Oauth2AccessTokens.h | Oauth2AccessTokens.cc | OAuth2 访问令牌表 |
| oauth2_refresh_tokens | Oauth2RefreshTokens.h | Oauth2RefreshTokens.cc | OAuth2 刷新令牌表 |

## 故障排除

### 问题 1: drogon_ctl 命令未找到
**症状**: `drogon_ctl: command not found`

**解决方案**:
```bash
# 检查 Drogon 安装
which drogon_ctl

# 如果未找到，重新安装 Drogon
# 使用 Conan
conan install drogon/[>=1.8.4]

# 或使用系统包管理器
# Ubuntu/Debian
sudo apt-get install drogon

# macOS
brew install drogon
```

### 问题 2: 无法连接到数据库
**症状**: `failed to connect to database` 或 `connection refused`

**解决方案**:
```bash
# 检查 PostgreSQL 服务
pg_isready -h localhost -p 5432

# 启动 PostgreSQL
# Windows
Start-Service postgresql*

# Linux
sudo systemctl start postgresql

# macOS
brew services start postgresql

# 验证数据库凭据
export PGPASSWORD='123456'
psql -h localhost -U oauth2_user -d oauth2_db -c "SELECT 1;"
```

### 问题 3: 表不存在
**症状**: `ERROR: relation "xxx" does not exist`

**解决方案**:
```bash
# 先重置数据库
/db-reset

# 然后验证表是否存在
export PGPASSWORD='123456'
psql -h localhost -U oauth2_user -d oauth2_db -c "\dt"
```

### 问题 4: 模型文件生成不完整
**症状**: 部分模型文件缺失或为空

**解决方案**:
```bash
# 检查 model.json 配置
cat OAuth2Server/model.json

# 确保 tables 数组包含所有需要的表
# 手动添加缺失的表名

# 重新执行生成
cd OAuth2Server
drogon_ctl create model .
```

### 问题 5: 编译错误
**症状**: 生成后编译失败，提示语法错误

**解决方案**:
```bash
# 检查 Drogon 版本
drogon_ctl version

# 确保 Drogon 版本 >= 1.8.0
# 如果版本过旧，更新 Drogon

# 检查生成的文件内容
head -50 Oauth2Clients.h

# 确认文件开头有正确的注释：
# "DO NOT EDIT. This file is generated by drogon_ctl"
```

### 问题 6: 字段类型不匹配
**症状**: 运行时错误，提示字段类型与数据库不符

**解决方案**:
```bash
# 查看数据库表结构
export PGPASSWORD='123456'
psql -h localhost -U oauth2_user -d oauth2_db -c "\d oauth2_clients"

# 如果表结构已更改，先删除旧表
# 然后重新执行 SQL 脚本
/db-reset

# 最后重新生成模型
/orm-gen
```

## 最佳实践

### 1. 表结构变更流程
```bash
# 1. 修改 migration SQL 脚本
vim OAuth2Server/sql/migrations/V002__oauth2_core.sql

# 2. 重置数据库
/db-reset

# 3. 重新生成 ORM 模型
/orm-gen

# 4. 重新编译项目
/build-and-test

# 5. 运行测试验证
cd build/OAuth2Server && ctest --output-on-failure
```

### 2. 备份策略
```bash
# 每次生成前自动备份
timestamp=$(date +%Y%m%d_%H%M%S)
backup_dir="models_backup_$timestamp"
mkdir -p $backup_dir
cp OAuth2Server/*.h OAuth2Server/*.cc $backup_dir/

# 保留最近 5 次备份
ls -td models_backup_* | tail -n +6 | xargs rm -rf
```

### 3. CI/CD 集成
```yaml
# .github/workflows/ci.yml
- name: Reset Database
  run: /db-reset

- name: Generate ORM Models
  run: /orm-gen

- name: Build Project
  run: /build-and-test

- name: Run Tests
  run: ctest --output-on-failure
```

### 4. 版本控制
```bash
# .gitignore 配置
# 自动生成的模型文件可以提交到版本控制
OAuth2Server/models/*.h
OAuth2Server/models/*.cc

# 但备份文件应该忽略
models_backup_*/

# model.json 应该提交
OAuth2Server/model.json
```

## 代码审查要点

生成模型后，需要检查：

1. ✅ **文件完整性**: 所有表都有对应的 .h 和 .cc 文件
2. ✅ **类定义正确**: 继承 Drogon ORM 基类
3. ✅ **字段映射**: 所有数据库字段都有对应的 getter/setter
4. ✅ **主键定义**: PrimaryKeyType 正确定义
5. ✅ **表名正确**: tableName 静态变量正确
6. ✅ **编译通过**: 项目能够成功编译
7. ✅ **测试通过**: 相关测试用例通过

## 安全注意事项

⚠️ **重要提示**:
1. ORM 生成的类**禁止手动修改**
2. 如需变更，应修改数据库表结构后重新生成
3. model.json 包含数据库密码，注意保护
4. 生成后的代码会覆盖现有文件，注意备份

## 相关技能

- `/db-reset` - 重置数据库（表结构变更前）
- `/build-and-test` - 生成后编译和测试
- `/e2e-test` - 端到端测试验证 ORM 模型

## 预期执行时间

- Windows (本地): ~5-10 秒
- Linux (本地): ~3-5 秒
- CI 环境: ~10-15 秒 (包含编译)

## 成功标志

✅ 成功执行后应看到：
```
✅ Old model files removed
Models generated successfully!
✅ All required model files generated
✅ Build successful
✅ Tests passed
```

且 `ls` 应显示所有 9 张表对应的模型文件（18 个文件：9 个 .h + 9 个 .cc）

## 技术细节

### Drogon ORM 模型结构

每个生成的模型类包含：

1. **列定义 (Cols struct)**: 所有列名的常量
2. **主键类型**: PrimaryKeyType 定义（用于查找）
3. **Getter 方法**: getValueOfXxx() 获取字段值
4. **Setter 方法**: setXxx() 设置字段值
5. **序列化方法**: toJson(), toString()
6. **CRUD 操作**: 通过 Mapper<model> 类调用

### 使用示例

```cpp
#include "models/Oauth2Clients.h"

using namespace drogon::orm;
using namespace drogon_model::oauth2_db;

// 查询客户端
Mapper<Oauth2Clients> mapper(dbClient);
mapper.findOne(
    Criteria(Oauth2Clients::Cols::_client_id, CompareOperator::EQ, "test_client"),
    [](const Oauth2Clients &client) {
        std::string clientId = client.getValueOfClientId();
        std::string secret = client.getValueOfClientSecret();
        // 使用客户端数据...
    },
    [](const DrogonDbException &e) {
        // 错误处理...
    });
```
