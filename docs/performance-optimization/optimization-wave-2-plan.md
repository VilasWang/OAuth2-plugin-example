# 第二轮（代码层）性能优化 — 技术方案与实施计划

> 状态：**已评审通过（修订版 v2，2026-08-21）** · 日期：2026-08-21 · 分支 `feat/competitor-benchmark`
> 评审：code-reviewer 子代理逐项代码核实（2 blocker + 4 major + 4 minor 全部采纳修复：失效钩子补全 3+6 处、装饰器落点改 libs/drogon/src、双形态键规范、constantTimeMemcmp 前提更正等）
> 依据：`performance-hotspot-instrumented-report.md` §4 量化杠杆表（仪器证据）+ `performance-optimization-report.md` §8 终表
> 前序已交付：第一轮（配置/DB 层 + LTO + reuse_port，见 `noncode-performance-optimization.md` 与终表）

## 0. 目标与总验收

| 目标 | 量化验收（同日背靠背 A/B，规则 3：QPS≥+5% 且 P99 不劣化 >5%，或 P99 改善 ≥10%） |
|---|---|
| S6 userinfo 追平/超越 Keycloak（33.3k，旧口径） | S6 稳态 ≥ +40%（18k → 25k+）；`pg_stat_statements` 复核 users/user_roles/roles 每请求 calls → ~0 |
| S2/S3 消除 validateClient 双查 | S3 ≥ +15%（仅 2 次 PG 往返消 1）；S2 ≥ +8%；`oauth2_clients` calls → ~0 |
| token 校验路径 Redis 单往返 | filter 阶段 ~960µs → ~500µs（stage probe 或 A/B 兜底） |
| tz 锁消除 | 先零代码实验（TZ=UTC）判定，目标 S6 +3~4% |

回归门（每杠杆全过才可提交合并）：full-test 8/8 + 静态检查 7 项 + **api-diff 零 BREAKING 漂移**（如出现 additive 公共面变更，走 `--update-baseline` 评审提交，无需版本 bump）+ 前端全量（若涉 API 行为不变则后端 8 步即可，见各杠杆说明）。

## 1. 杠杆清单（优先级 = 收益/风险比排序）

### P0 — validateClient 缓存行本地校验（消 S2/S3 每请求 1 次 PG 往返）

**仪器依据**：S3 每请求 `oauth2_clients` 点查 1 次（0.016ms 执行 + ~670µs 往返）；S2 同（0.048ms）。缓存序列化早已包含 hash+salt（`RedisCachedClientRepository.cc:26-29` 注释明示），仅校验逻辑透传 PG。

**设计**：
- `RedisCachedClientRepository::validateClient`（现透传点 `:226-234`）改为：经本装饰器的 `getClient` 取缓存行 → 命中则**本地执行与 PG 路径完全同构的校验**（蓝本 `PostgresClientRepository.cc:190-251`：PUBLIC 客户端免密放行、CONFIDENTIAL 空 secret 拒绝、`sha256(secret+salt)` 小写归一、`constantTimeMemcmp` 常量时间比较 + 长度等价检查——四条缺一不可）；未命中 → 回源 PG 现行路径 + 回填（复用 getClient 的 fill 逻辑，使后续校验命中）。
- `constantTimeMemcmp` **已在共享处**：`libs/common/include/authforge/common/utils/ConstantTimeCompare.h:22`（F-004 已完成去重迁移，api-diff 基线已含该符号）——P0 无任何 api-diff 动作，直接 include 即用。~~迁移~~/静态副本方案作废（后者是对 F-004 的倒退）。
- **行为变化（须显式接受）**：现状 secret 校验每请求实时查 PG（轮换/删除即时生效）；改后走缓存行（TTL 300s）→ 引入最长 300s 陈旧窗口。
- **失效钩子（全部 3 个客户端写路径，一个不能少）**：`libs/drogon/src/admin/ClientManagementService.cc` 的 `updateClient`（:311）、**`deleteClient`（:444）**、**`updateClientScopes`（:569，allowedScopes 是缓存序列化字段）** —— 各自成功回调后 fire-and-forget `DEL authforge:cache:client:<id>`。删除语义：DEL 丢失时已删客户端凭据最长 300s 仍可过校验（TTL 兜底）—— 已入风险表显式评审。
- 触点：`libs/storage-redis/src/RedisCachedClientRepository.cc`、`ClientManagementService.cc`（DEL ×3）。

**A/B**：S2+S3 阶梯同日两臂。**单测**：命中路径正确/错误 secret、PUBLIC 客户端、secret 轮换后 DEL 即时生效、Redis 故障软回退 PG。

### P1 — S6 用户资料/角色 Redis 缓存 + 写路径失效（消 3 次 PG 往返，最大收益项）

**仪器依据**：S6 每请求 3 次 PG 往返（roles 2 查 1,416µs + profile 1 查 667µs）= 端到端 54%；执行器合计仅 54µs。

**设计**：
- 键与**标识符规范化（评审 M2）**：读路径入参是 subject 字符串，存在**数字内部 id 与 public_sub 两种形态**（`PostgresIdentityRepository.cc:634-654` / `OAuth2Plugin.cc:768-805` 的 stoi 双分派）。规范：**缓存键 = 读入参的原样形态**（`authforge:cache:user:profile:<subject>` / `:roles:<subject>`）。失效时写路径从 users 行同时取数字 id 与 public_sub，**DEL 两种形态 × 两种键 = 4 键**（fire-and-forget），确保非数字 subject 的条目可达。
- 值：profile = JSON（id/username/email/email_verified）；roles = JSON 数组（**角色名** `vector<string>`，与 `PostgresIdentityRepository.cc:540-543` 返回一致）。
- TTL 接线（评审 m3）：`cache.ttl_seconds.user_profile`（默认 300）/ `user_roles`（默认 120），对齐 `OAuth2Plugin.cc:298-305` 既有模式，不硬编码。
- 负缓存（评审 m1，偏离 client 模式的显式决定）：用户不存在 60s —— 偏离 `RedisCachedClientRepository.cc:144-148` 的"不缓存 miss"惯例，理由：public_sub 在注册时才铸造、60s 影子窗口对 userinfo 语义无害（读场景，非凭据校验）。
- **落点（评审 B2）**：装饰器放 **`libs/drogon/src`** 内部（插件装配点 `OAuth2Plugin.cc:349-354` 处包装 roleRepo_/userRepo_ 读），**不新增任何公共头、零 api-diff 动作**。`apps/server/src/bootstrap/IdentityAssembly.cc:67-68` 的第二个 PostgresIdentityRepository 实例（login/MFA/WebAuthn/social 用）**有意不装饰**（不在 S6 热路径）。
- **失效钩子清单（承重墙；写路径 DEL，非仅 TTL；评审 M1/M3 补全）**：
  1. admin 用户资料更新（users UPDATE）
  2. admin 角色分配/移除（user_roles INSERT/DELETE）
  3. 用户自助资料修改
  4. 软删除/锁定（findById/findByPublicSub 过滤 `deleted_at IS NULL`，软删除后必须 DEL）
  5. **邮箱验证成功（`EmailVerificationService.cc:164-180`，email_verified 是缓存字段）**
  6. **roles 表删除/更名（`RoleScopeAdminService.cc:311`，V005 `ON DELETE CASCADE`）——接受 ≤120s 陈旧**（无 role→users 反向索引，反向 DEL 需额外查询；显式评审决定，见风险表）
- **影响面（评审 M3，正确性门必须覆盖）**：roles 缓存不止喂 userinfo claims —— 还喂 `AuthorizationFilter.cc:183-186`（**admin API 的 RBAC 门**）、token 签发链（`OAuth2Plugin.cc:186` StorageRoleProvider → TokenService）、`IdentityService.cc:203` validateUserRolesForScopes。
- **不在缓存范围的**：密码哈希、MFA 状态、登录计数（安全态不走 TTL 缓存）。

**A/B**：S6 阶梯（目标 ≥+40%）。**正确性门**：角色撤销 → userinfo 下一次请求即反映（集成测试）；full-test 8/8（59+52 端点含用户/角色管理流）。**仪器复核**：pg_stat_statements 三查询 calls→~0。

### P2 — token 校验 Redis 双往返合并（EXISTS revoked + GET access → EVAL 单往返）

**仪器依据**：filter 阶段 960µs = 两次串行 Redis 往返（各 ~450µs）+ 43µs。

**设计**：`RedisCachedTokenRepository::getAccessToken` 的 EXISTS→GET 串行链（`:172-244`）改为一段 Lua：`local r=redis.call('EXISTS',KEYS[1]); if r>0 then return {1,''} end; return {0,redis.call('GET',KEYS[2])}`（EVAL 一次往返，语义等价：revoked 优先）。`introspectToken` 同构路径同改。drogon `execCommandAsync` 支持 `EVAL %s %d %s %s`（先例：`RedisGrantRepository.cc:178/263`）。**实现注意（评审 m2）**：GET miss 时 Lua 返回数组内含 nil —— RedisResult 需按"revoked / 未撤销+有值 / 未撤销+空"三分处理（无既有先例，单测覆盖）。

### P3 — glibc tz 锁消除（~4% 线程样本）

**仪器依据**：35 轮采样中 ~20/520 叶子帧阻塞于 `__tz_convert`/`__tzfile_read`（三方库 localtime 类调用；一方代码 grep 零命中）。

**设计（三步走，先实验后动刀）**：
1. **零代码实验**：bench compose backend 服务加 `TZ=UTC` env → S6 同日 A/B。有效（≥+3%）则确认为真瓶颈，进入 2；无效则关闭本杠杆（说明锁开销在别处）。
2. **定位**：非 LTO Debug 构建或 ltrace/gdb（配方见 instrumented report §3.2）找精确调用方。
3. **修复**：按调用方选（缓存格式化时间串 / `gmtime_r` 替代 / Drogon-trantor 层规避）。
**注意**：TZ=UTC 会改变日志时间戳时区（bench 可接受；prod 不改，仅作实验变量）。

### P4 — 双缓存键合并读取（依赖 P1 落地后才有意义）

P1 后 S6 剩余 = 2 Redis 往返（token）+ 2 Redis 往返（profile+roles）+ 框架残差。将 profile/roles 两键改 **MGET 单往返**（与 P2 叠加后 S6 理论 = 2 次 Redis 往返 + ~0.8ms 残差）。**先做 P2 再评估**。

## 2. 实施顺序与里程碑

| 里程碑 | 内容 | 依赖 |
|---|---|---|
| M1 | P0 validateClient | 无（最低风险先行，验证 A/B 管道） |
| M2 | P1 S6 缓存 + 失效钩子 | 无（核心收益） |
| M3 | P2 Lua 合并 + P3 TZ 实验 | P2 独立；P3 步骤1 独立 |
| M4 | P4 MGET（视 P2 后数据） + 四产品对比重跑（对外表更新） | P0-P3 判定后 |

每杠杆：单变量提交 → 同日 A/B → 规则 3 判定 → 回归门 → 结果入库（RESULTS_DIR 隔离）→ 文档回填（本文件状态列 + instrumented report）。

## 3. 统一工程纪律（沿用第一轮 SOP）

- 一次提交一个变量；A/B 同日背靠背；判定只认同日对；bench 期间宿主机零并发任务（含只读子代理）。
- 回归门顺序：bench A/B 全部完成 → full-test（串行铁律）→ 静态 7 项。
- 治理面：无端点/语义变化 → OpenAPI 不动；装饰器头文件放 `libs/drogon/src` 内部（零公共面变更，零 api-diff 动作）；版本号不 bump（纯性能轮）。
- 回滚：任一杠杆否决/出问题 → revert 对应单提交（第一轮先例：TTL jitter b55dc46→0c49c46）。
- 机器陷阱清单照旧（Redis 僵尸、`(wrk &)` 不可用、镜像项目名、ctest 串行）。

## 4. 风险与对策

| 风险 | 对策 |
|---|---|
| P0 引入 secret/scope 陈旧窗口 | **全部 3 个写路径**（update/delete/updateScopes）挂 DEL 对齐即时语义；DEL 丢失时 TTL 300s 兜底（已删客户端凭据最长 300s 可过）—— 评审显式接受 |
| P1 失效钩子遗漏 → 角色撤销延迟（安全） | 钩子清单 6 类逐写路径核实 + "改角色→userinfo/admin API 均立即反映"集成测试为硬门（覆盖 AuthorizationFilter RBAC 门与签发链两个消费面） |
| P1 roles 表删除/更名无逐用户失效 | 无 role→users 反向索引 —— **接受 ≤120s 陈旧**（显式评审决定）；如不可接受再补反向查询失效 |
| P1 双形态键（数字 id / public_sub）DEL 不可达 | 写路径从 users 行取双形态，DEL 4 键；集成测试用 public_sub token 验证撤销可达 |
| 常量时间比较在新路径退化（时序侧信道） | 复用 `ConstantTimeCompare.h:22` 同一实现 + 单测断言比较时长与输入无关（松散上界） |
| Lua 脚本与 Redis 集群/主从语义 | EVAL 单键单脚本（无跨槽）；bench 单实例验证，prod 部署文档注记 |
| 缓存击穿/雪崩 | TTL 已短（120/300s）+ 负缓存 + 软失败；不做 single-flight（本机已证伪雷群假设，不追加复杂度） |
| TZ 实验污染日志时间戳可读性 | 仅 bench 环境；prod 不动 |

## 5. 明确不做（本轮）

S5 写链 CTE 合并（事务语义另立专项）；JWT/ES256 档位；L1 CachedClientRepository 接线（失效语义未设计）；is_fast db/redis client；CI 性能回归门禁（需专用 runner）；审计采样分级。

## 6. 状态跟踪

| 杠杆 | 状态 | 判定 | 提交 |
|---|---|---|---|
| P0 validateClient | **已实施（2026-08-21）** | **胜出**：S2 steady 10,685→12,766（+19.5%）、S3 16,436→19,876（+20.9%），同日 A/B；新单测 12 断言过；full-test 8/8 + 静态 7/7 + api-diff 零漂移 | 8eccc7c |
| P1 S6 缓存 | **已实施（2026-08-21）** | **胜出**：S6 steady 15,045→35,750（**+137%**，2.37x），全档 +61~142%，超阈值（≥+40%）三倍；pg_stat_statements 复核 S6 负载下三查询 **零调用**；S6 已超 Keycloak 33,347（旧口径）；3 条新单测 25 断言过；full-test 8/8 + 静态 7/7 | 657ff77 |
| P2 Lua 合并 | 待实施 | — | — |
| P3 TZ | 待实验 | — | — |
| P4 MGET | 待评估 | — | — |
