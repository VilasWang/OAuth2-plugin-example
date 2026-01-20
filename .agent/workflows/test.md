---
description: 执行完整的单元测试和集成测试流程
---

# 测试执行流程

## 前置条件检查

// turbo

1. 确认构建目录存在

```powershell
cd d:\work\development\Repos\backend\drogon-plugin\OAuth2-plugin-example\OAuth2Backend
Test-Path .\build\test\Release\OAuth2Test_test.exe
```

1. 如果可执行文件不存在，先执行构建

```powershell
.\build.bat -release
```

## 执行测试

// turbo
3. 运行 CTest（包含所有已注册的测试）

```powershell
cd d:\work\development\Repos\backend\drogon-plugin\OAuth2-plugin-example\OAuth2Backend\build
ctest -C Release --output-on-failure --verbose
```

// turbo
4. 或者直接运行测试可执行文件查看详细输出

```powershell
cd d:\work\development\Repos\backend\drogon-plugin\OAuth2-plugin-example\OAuth2Backend\build\test\Release
.\OAuth2Test_test.exe
```

## 运行特定测试

// turbo
5. 仅运行单元测试（不需要数据库）

```powershell
.\OAuth2Test_test.exe -r ConfigTest
.\OAuth2Test_test.exe -r StorageTest
.\OAuth2Test_test.exe -r PluginTest
```

// turbo
6. 运行集成测试（需要 PostgreSQL/Redis）

```powershell
.\OAuth2Test_test.exe -r PostgresStorageTest
.\OAuth2Test_test.exe -r RedisStorageTest
.\OAuth2Test_test.exe -r IntegrationE2E
```

// turbo
7. 运行用户系统测试

```powershell
.\OAuth2Test_test.exe -r UserSystemTest
```

## 测试结果说明

| 测试名称 | 类型 | 依赖 |
|---------|------|------|
| ConfigTest | 单元测试 | 无 |
| StorageTest | 单元测试 | 无 |
| MemoryStorageTest | 单元测试 | 无 |
| PluginTest | 单元测试 | 无 |
| AdvancedStorageTest | 单元测试 | 无 |
| PostgresStorageTest | 集成测试 | PostgreSQL |
| RedisStorageTest | 集成测试 | Redis |
| UserSystemTest | 集成测试 | PostgreSQL |
| IntegrationE2E | E2E测试 | PostgreSQL |

## 失败处理

- 如果测试失败超过 3 次且错误相同，停止重试
- 分析根本原因，检查：
  - 数据库连接是否正常
  - 配置文件 `config.json` 是否正确
  - 表结构是否匹配 ORM 模型
