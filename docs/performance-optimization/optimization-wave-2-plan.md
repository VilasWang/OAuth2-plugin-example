# 第二轮（代码层）性能优化 — 技术方案与实施计划

> 状态：待评审 → 实施 · 日期：2026-08-21 · 分支 `feat/competitor-benchmark`
> 依据：`performance-hotspot-instrumented-report.md` §4 量化杠杆表（仪器证据）+ `performance-optimization-report.md` §8 终表
> 前序已交付：第一轮（配置/DB 层 + LTO + reuse_port，见 `noncode-performance-optimization.md` 与终表）

## 0. 目标与总验收

| 目标 | 量化验收（同日背靠背 A/B，规则 3：QPS≥+5% 且 P99 不劣化 >5%，或 P99 改善 ≥10%） |
|---|---|
| S6 userinfo 追平/超越 Keycloak（33.3k，旧口径） | S6 稳态 ≥ +40%（18k → 25k+）；`pg_stat_statements` 复核 users/user_roles/roles 每请求 calls → ~0 |
| S2/S3 消除 validateClient 双查 | S3 ≥ +15%（仅 2 次 PG 往返消 1）；S2 ≥ +8%；`oauth2_clients` calls → ~0 |
| token 校验路径 Redis 单往返 | filter 阶段 ~960µs → ~500µs（stage probe 或 A/B 兜底） |
| tz 锁消除 | 先零代码实验（TZ=UTC）判定，目标 S6 +3~4% |

回归门（每杠杆全过才可提交合并）：full-test 8/8 + 静态检查 7 项 + api-diff 零漂移 + 前端全量（若涉 API 行为不变则后端 8 步即可，见各杠杆说明）。

## 1. 杠杆清单（优先级 = 收益/风险比排序）

### P0 — validateClient 缓存行本地校验（消 S2/S3 每请求 1 次 PG 往返）

**仪器依据**：S3 每请求 `oauth2_clients` 点查 1 次（0.016ms 执行 + ~670µs 往返）；S2 同（0.048ms）。缓存序列化早已包含 hash+salt（`RedisCachedClientRepository.cc:26-29` 注释明示），仅校验逻辑透传 PG。

**设计**：
- `RedisCachedClientRepository::validateClient`（现透传点 `:226-234`）改为：经本装饰器的 `getClient` 取缓存行 → 命中则**本地执行与 PG 路径完全同构的校验**（蓝本 `PostgresClientRepository.cc:190-251`：PUBLIC 客户端免密放行、`sha256(secret+salt)` 小写归一、`constantTimeMemcmp` 常量时间比较 + 长度等价检查）；未命中 → 回源 PG 现行路径 + 回填。
- `constantTimeMemcmp` 现为 PostgresClientRepository.cc 内部实现 —— 提升到 `authforge::common` utils（两库共享，公共头新增会触发 api-diff —— **方案：放 `libs/common` 既有 utils 头内的新增函数**，api-diff 基线需同步更新，属"新增公共符号"流程，与 SDK 面无破坏；若评审认为不妥则降级为两库各自 src 内静态副本）。
- **行为变化（须显式接受）**：现状 secret 校验每请求实时查 PG（轮换即时生效）；改后走缓存行（TTL 300s）→ 引入最长 300s 陈旧窗口。**对齐现状语义的做法**：admin 更新客户端 secret/状态的写路径挂 DEL `authforge:cache:client:<id>`（getClient 缓存本无失效，这是补上缺口，属净改善）。
- 触点：`libs/storage-redis/src/RedisCachedClientRepository.cc`、`libs/storage-postgres/src/PostgresClientRepository.cc`（比较逻辑抽出）、client 更新写路径（实现时 grep `updateClient` 定位，挂 DEL）。

**A/B**：S2+S3 阶梯同日两臂。**单测**：命中路径正确/错误 secret、PUBLIC 客户端、secret 轮换后 DEL 即时生效、Redis 故障软回退 PG。

### P1 — S6 用户资料/角色 Redis 缓存 + 写路径失效（消 3 次 PG 往返，最大收益项）

**仪器依据**：S6 每请求 3 次 PG 往返（roles 2 查 1,416µs + profile 1 查 667µs）= 端到端 54%；执行器合计仅 54µs。

**设计**：
- 键：`authforge:cache:user:profile:<userId>`（JSON：id/username/email/email_verified，TTL **300s**）；`authforge:cache:user:roles:<userId>`（JSON 数组，TTL **120s**）。负缓存（不存在用户）60s，C7 式 TTL 守卫，软失败回源 —— 全部对齐 `RedisCachedClientRepository` 既有模式。
- 落点：装饰 identity 读路径。`OAuth2Plugin::getUserRoles`（`OAuth2Plugin.cc:634` → identityService_）与 `getUserInfo`（`:750` → userRepo_->findById）。实现取 identity 层装饰器（`libs/storage-redis` 新增 `RedisCachedUserInfoRepository` 风格装饰，src 内部头，**不新增公共头**）。
- **失效设计（承重墙，写路径 DEL，非仅 TTL）**：roles 进 userinfo claims 且客户端可能用作粗粒度授权 → 角色绑定变更必须即时生效。失效钩子触点（实现时逐一 grep 核实并补集成测试）：admin 用户资料更新、admin 角色分配/移除、用户自助资料修改、软删除/锁定。DEL 两键（profile+roles）于同一写事务成功回调后发 fire-and-forget。
- **不在缓存范围的**：密码哈希、MFA 状态、登录计数（安全态不走 TTL 缓存）。

**A/B**：S6 阶梯（目标 ≥+40%）。**正确性门**：角色撤销 → userinfo 下一次请求即反映（集成测试）；full-test 8/8（59+52 端点含用户/角色管理流）。**仪器复核**：pg_stat_statements 三查询 calls→~0。

### P2 — token 校验 Redis 双往返合并（EXISTS revoked + GET access → EVAL 单往返）

**仪器依据**：filter 阶段 960µs = 两次串行 Redis 往返（各 ~450µs）+ 43µs。

**设计**：`RedisCachedTokenRepository::getAccessToken` 的 EXISTS→GET 串行链（`:172-244`）改为一段 Lua：`local r=redis.call('EXISTS',KEYS[1]); if r>0 then return {1,''} end; return {0,redis.call('GET',KEYS[2])}`（EVAL 一次往返，语义等价：revoked 优先）。`introspectToken` 同构路径同改。drogon `execCommandAsync` 支持 `EVAL %s %d %s %s`。
**A/B**：S3+S6；stage probe 复核（可在实施时临时复用插桩配方）。

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
- 治理面：无端点/语义变化 → OpenAPI 不动；缓存装饰器头文件放 src 内部（公共头新增仅 P0 的 constantTimeMemcmp 场景，走 api-diff 基线更新流程）；版本号不 bump（纯性能轮）。
- 回滚：任一杠杆否决/出问题 → revert 对应单提交（第一轮先例：TTL jitter b55dc46→0c49c46）。
- 机器陷阱清单照旧（Redis 僵尸、`(wrk &)` 不可用、镜像项目名、ctest 串行）。

## 4. 风险与对策

| 风险 | 对策 |
|---|---|
| P0 引入 secret 陈旧窗口 | 写路径 DEL 对齐即时语义；评审显式确认可接受范围 |
| P1 失效钩子遗漏 → 角色撤销延迟（安全） | 钩子清单逐写路径 grep 核实 + "改角色→userinfo 立即反映"集成测试为硬门 |
| 常量时间比较在新路径退化（时序侧信道） | 复用同一 constantTimeMemcmp 实现 + 单测断言比较时长与输入无关（松散上界） |
| Lua 脚本与 Redis 集群/主从语义 | EVAL 单键单脚本（无跨槽）；bench 单实例验证，prod 部署文档注记 |
| 缓存击穿/雪崩 | TTL 已短（120/300s）+ 负缓存 + 软失败；不做 single-flight（本机已证伪雷群假设，不追加复杂度） |
| TZ 实验污染日志时间戳可读性 | 仅 bench 环境；prod 不动 |

## 5. 明确不做（本轮）

S5 写链 CTE 合并（事务语义另立专项）；JWT/ES256 档位；L1 CachedClientRepository 接线（失效语义未设计）；is_fast db/redis client；CI 性能回归门禁（需专用 runner）；审计采样分级。

## 6. 状态跟踪

| 杠杆 | 状态 | 判定 | 提交 |
|---|---|---|---|
| P0 validateClient | 待实施 | — | — |
| P1 S6 缓存 | 待实施 | — | — |
| P2 Lua 合并 | 待实施 | — | — |
| P3 TZ | 待实验 | — | — |
| P4 MGET | 待评估 | — | — |
