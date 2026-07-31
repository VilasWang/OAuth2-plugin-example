# catch 类型合规扫描报告

生成时间：2026-07-25
扫描范围：整个项目（排除 test/ 和 ORM 生成 models/）
规则：`DrogonDbException`/`RedisException` 仅作为异步回调参数有效，同步 `try-catch` 应使用 `catch (const std::exception &e)` + `catch (...)`。

---

## 扫描结果总览

| 搜索模式 | 命中 | 生产中 | 误报（回调参数） | 需修复 |
|----------|------|--------|:---:|:---:|
| `catch (const DrogonDbException` | 6 | 3 | 0 | **3** |
| `catch (const RedisException` | 0 | 0 | — | 0 |
| `catch (DrogonDbException` (无 const) | 0 | 0 | — | 0 |
| `catch (RedisException` (无 const) | 0 | 0 | — | 0 |

---

## 需修复项：3 处（全部在 `libs/drogon/src/AuthService.cc`）

### #1: AuthService.cc:186 — `validateUser()` 外部 try-catch

```cpp
// 当前：第 186 行
    catch (const DrogonDbException &e)
    {
        LOG_WARN << "Validate User Init Failed: " << e.base().what();
        (*sharedCb)(std::nullopt);
    }

// 应改为：
    catch (const std::exception &e)
    {
        LOG_WARN << "Validate User Init Failed: " << e.what();
        (*sharedCb)(std::nullopt);
    }
    catch (...)
    {
        LOG_WARN << "Validate User Init Unknown Exception";
        (*sharedCb)(std::nullopt);
    }
```

**上下文**（第 184 行 `)` 闭合了一个 `mapper.findOne()` 调用，第 186 行的 catch 包裹的是 Mapper 构造 + `findOne` 调用的 setup 阶段，而非回调参数。

---

### #2: AuthService.cc:300 — `registerUser()` 外部 try-catch

```cpp
// 当前：第 300 行
    catch (const DrogonDbException &e)
    {
        LOG_ERROR << "Register Init Failed: " << e.base().what();
        (*sharedCb)("INTERNAL_ERROR");
    }

// 应改为：
    catch (const std::exception &e)
    {
        LOG_ERROR << "Register Init Failed: " << e.what();
        (*sharedCb)("INTERNAL_ERROR");
    }
    catch (...)
    {
        LOG_ERROR << "Register Init Unknown Exception";
        (*sharedCb)("INTERNAL_ERROR");
    }
```

**上下文**（第 298 行 `)` 闭合了 `mapper.insert()` 调用。`DrogonDbException` 只能 catch Drogon ORM 返回的 SQL-layer 异常；Mapper 构造时连接池获取失败、网络断开等会抛 `std::system_error` 或其他非 Drogon 异常，被 `DrogonDbException` 漏掉 → propagate → crash。

---

### #3: AuthService.cc:404 — `getUserInfo()` 外部 try-catch

```cpp
// 当前：第 404 行
    catch (const DrogonDbException &e)
    {
        LOG_WARN << "Get User Info Init Failed: " << e.base().what();
        (*sharedCb)(std::nullopt);
    }

// 应改为：
    catch (const std::exception &e)
    {
        LOG_WARN << "Get User Info Init Failed: " << e.what();
        (*sharedCb)(std::nullopt);
    }
    catch (...)
    {
        LOG_WARN << "Get User Info Init Unknown Exception";
        (*sharedCb)(std::nullopt);
    }
```

**上下文**（第 402 行 `)` 闭合了 `mapper.findBy()` 调用。

---

## 已确认正确的目录（0 处漏网）

| 目录 | 状态 |
|------|:---:|
| `OAuth2Plugin/src/storage/` | ✅ PostgresTokenRepository 已在之前修复为 `std::exception` + `...` |
| `libs/storage-postgres/src/` | ✅ PostgresIdentityRepository 使用 `catch (...)`，无 DrogonDbException |
| `libs/drogon/src/services/` | ✅ 无 DrogonDbException catch |
| `libs/drogon/src/admin/` | ✅ 无 DrogonDbException catch（使用 `catch (...)`） |
| `libs/drogon/src/controllers/` | ✅ OAuth2StandardController 等已修复 |
| `libs/oauth2/src/` | ✅ 无 DrogonDbException catch |

---

## 影响分析

这 3 处是点对点端点测试中服务器间歇性 crash 的**直接根因**：

```
端点测试 → Mapper<Users>(dbClient_); 构造失败
    ↓ (连接池满/网络抖动 → std::system_error)
catch (const DrogonDbException &e)  ← 类型不匹配，漏掉
    ↓
std::system_error 沿栈逃逸
    ↓
Drogon 事件循环捕获未处理异常 → 进程退出
    ↓
端点测试看到 "Connection Refused"
```

这三个方法（`validateUser`、`registerUser`、`getUserInfo`）覆盖了 OAuth2/admin 端点中大量测试用例的认证/注册/用户信息查询路径，解释了 crash 在特定测试区间出现的原因。
