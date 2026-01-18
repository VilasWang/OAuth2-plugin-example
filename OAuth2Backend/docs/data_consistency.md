# OAuth2 数据一致性与并发控制 (Data Consistency)

## 1. 背景与问题

在 OAuth2 的 `authorization_code` 授权码模式中，核心安全要求是 **"Authorization Code 只能使用一次"** (Single Use)。

即使客户端并发发起多次请求（例如网络重试或恶意攻击），服务器必须保证：

1. **仅有一次** 能够成功换取 Access Token。
2. 后续请求必须失败，并触发安全警告（Replay Attack）。
3. 任何情况下不允许多个 Token 被颁发给同一个 Code。

传统的 "Check-Then-Act" (先查询后更新) 逻辑在并发下是不安全的：

- 线程 A 查询 Code: `used = false` (OK)
- 线程 B 查询 Code: `used = false` (OK)
- 线程 A 更新 `used = true` -> 颁发 Token A
- 线程 B 更新 `used = true` -> 颁发 Token B
- **结果**: "双花" (Double Spending)，严重安全漏洞。

## 2. 解决方案：Atomic Consume (原子消费)

本系统在 `IOAuth2Storage` 接口层引入了 `consumeAuthCode(code)` 方法，强制要求存储后端实现 **"检查并更新" (Check-and-Set)** 的原子操作。

### 2.1 业务流程重构

`OAuth2Plugin::exchangeCodeForToken` 的逻辑已重构为：

1. **Atomic Consume**: 调用存储层的 `consumeAuthCode`。
2. **Result Check**:
   - 如果返回 `nullopt`: 说明 Code 不存在 **或者** 已经被使用。拒绝请求 (`invalid_grant`)。
   - 如果返回 `AuthCode对象`: 说明当前线程成功"抢占"了该 Code，且状态已更新为 `used=true`。
3. **Validation**: 校验 Client ID、过期时间等。
4. **Issue Token**: 生成并颁发 Token。

### 2.2 后端实现细节

#### PostgreSQL (UPDATE ... RETURNING)

利用 SQL 的原子更新能力。`WHERE used = false` 是关键。

```sql
UPDATE oauth2_codes 
SET used = true 
WHERE code = $1 AND used = false 
RETURNING client_id, user_id, scope, redirect_uri, expires_at;
```

- 如果 Code 未使用: 更新成功，返回 1 行数据。
- 如果 Code 已使用: `WHERE` 条件不满足，更新 0 行，返回空结果。

#### Redis (Lua Script)

利用 Redis 的单线程特性和 Lua 脚本的原子性。

```lua
local key = KEYS[1]
local val = redis.call('GET', key)
if not val then return nil end        -- 不存在

local json = cjson.decode(val)
if json.used then return nil end      -- 已使用 (Replay)

json.used = true                      -- 标记为已使用
local newVal = cjson.encode(json)
redis.call('SET', key, newVal)        -- 写回
return newVal                         -- 返回更新后的数据
```

#### Memory (Mutex)

使用 `std::recursive_mutex` 保证同一进程内的线程安全。

```cpp
std::lock_guard<std::recursive_mutex> lock(mutex_);
auto it = authCodes_.find(code);
if (it != authCodes_.end() && !it->second.used) {
    it->second.used = true;
    return it->second;
}
return std::nullopt;
```

## 3. 测试验证

系统包含专门的并发与重放测试用例：

- `PluginTest.cc`: `TestReplayAttack` 模拟同一次 Code 的两次交换请求，验证第二次必然失败。
- `AdvancedStorageTest.cc`: 验证底层存储对已撤销/已过期数据的拒绝逻辑。
