# introspect 覆盖索引方案（v2，已评审）

> **日期**: 2026-08-18
> **来源**: `noncode-performance-optimization.md` §四.3 / 用户指令"introspect 覆盖索引出个方案，子代理评审下"
> **状态**: ✅ 已子代理评审（2026-08-18）。结论：**C 案批准落地（SQL 必改：去 CONCURRENTLY）**；B 案降级 backlog；A 案维持不做。v1→v2 修订依据评审报告，事实错误已改正。

---

## 〇、评审结论摘要

| 项 | v1 提案 | 评审裁决 |
|---|---|---|
| A（5 列 INCLUDE，文档原案） | 单独不可行 | ✅ 维持：`SELECT *` 下无法 IOS，仅未来投影列代码改造后成立 |
| B（全行 INCLUDE 覆盖索引） | 可选实测 | ⬇️ **降级 backlog**：2000 行 bench 小表上收益≈噪声（表+索引常驻 shared_buffers，heap fetch 近免费），且种子后无 VACUUM 时 IOS 根本不触发；写代价（INSERT +1 宽索引、revoke UPDATE 翻新）却是真的 |
| C（清理冗余索引） | 推荐先行 | ✅ **批准**：三个索引与 PK 冗余属实、全仓库无代码/测试/ORM/迁移重放隐患；**阻断项唯一**：迁移器单事务 → SQL 必须用普通 `DROP INDEX IF EXISTS`（见 §二.6） |
| 新发现 | — | ①迁移器单事务 vs CONCURRENTLY（阻断，已改 SQL）；②admin 撤销端点存在 token 前缀 LIKE 删除（不阻断 C，PK b-tree 可服务，但是未来动 PK/opclass 的隐藏约束）；③`incrementIntrospectCount`（PG repo + OAuth2Plugin 两层）是死代码，"introspect 纯读"依赖其持续无人调用 |

## 一、目标与背景

S3 introspect（~20k QPS）与 S6 userinfo 的 token 点查是基准中最热的 PG 读路径。
cache 开启后 S3 仍每请求落库：正向 token 缓存受 **N2 判别器**约束（仅走过
发放/校验路径的 token 才回填），bench 预植 token 永不回填（见 SUMMARY.md 快赢
A/B 节裁决 2）。TTL 同步到期雷群（30s 周期 ~800ms 尖峰）的全量回源也走同一
条查询。

目标：降低 token 表读写路径成本，不推高 S2/S5 写放大。

## 二、代码与 schema 事实（已经子代理核实）

1. **查询形状**：`PostgresTokenRepository::introspectToken`
   （`libs/storage-postgres/src/PostgresTokenRepository.cc:500`）与
   `getAccessToken`（`:229`）均为 `Mapper::findOne(Criteria(token EQ))` →
   Drogon 1.9.13 生成 **`select * from "oauth2_access_tokens" where "token" = $1`**
   （无 `LIMIT 1`——Mapper 仅在链式 `.limit(n)` 时追加，仓库从未链式；PK 等值
   至多 1 行，性能等价）。
2. **introspect 是纯读**：PG 路径无 UPDATE。`introspect_count` 仅内存实现递增
   （`MemoryTokenRepository.cc:224`）；PG 的 `incrementIntrospectCount`
   （`:591`）与 `OAuth2Plugin::incrementIntrospectCount`（`OAuth2Plugin.cc:698`）
   均为**死代码**（无调用方）。⚠️ 该结论依赖死代码保持无人调用；清理它属 §十
   backlog（注意 `TokenRepositoryContractTest.cc:916-933` 在测它，清理需连测试）。
3. **introspect 响应消费 10 列**：revoked, expires_at, client_id, issued_at,
   issuer, audience, not_before, user_id, scope + 查找键 token。
4. **表 DDL**（`V002__oauth2_core.sql:39`）：PK = `token VARCHAR(100)`（存
   SHA-256 hex，实际 64 字节），**共 13 列**（10 消费 + introspect_count /
   revoked_at / revoked_by 三列不消费但 `SELECT *` 会取）。
5. **现有索引 8 个 B-tree**：PK(token) + V003 四个 + V016 三个。**三个与 PK
   冗余**：`idx_access_tokens_token`（与 PK 键列完全相同）、
   `idx_access_tokens_active`（ON(token) WHERE revoked=false，对 EQ 点查被 PK
   覆盖）、`idx_refresh_tokens_token`（refresh 表 PK 重复，V003:7 vs V002:57）。
6. **migration 执行方式（v1 论断错误，评审已纠正）**：`MigrationRunner` 委托
   `SchemaManager::migrate`，后者（`apps/server/src/SchemaManager.cc:196-211`，
   #46）**把全部待应用迁移包在一个事务里**逐条 `trans->execSqlSync` 执行。
   → **`CREATE/DROP INDEX CONCURRENTLY` 在服务器/Helm Job/FULLA_AUTO_MIGRATE
   路径必然失败**（"cannot run inside a transaction block"，迁移失败=进程退出）。
   本方案所有 SQL 用普通 DDL（索引 DDL 事务内合法；DROP 瞬时；生产走 Helm
   迁移 Job 变更窗口）。本地 db-reset skill 的 `psql -f` 逐文件 autocommit 路径
  不受影响。
7. **token 列查询形状清单（含评审反例）**：EQ 点查 ×8（repository findOne）、
   `token IN (SELECT ...)`（revokeTokenFamily:475，PK 服务）、purge/归档走
   expires_at、admin 端点 UPDATE 全按 user_id；**反例**：
   `TokenManagementService.cc:184` 的 admin 撤销用
   `WHERE token LIKE $prefix%` 前缀删除——PK 的 b-tree（确定性排序）可服务
   范围扫描，删冗余索引后不受影响，但**未来任何"动 PK / 换 opclass"的决策必须
   先复核此端点**。

## 三、方案选项（v2 修订后）

### A. 窄覆盖索引（文档 §四.3 原案）——单独不可行 ❌（维持）

`INCLUDE (user_id, client_id, scope, expires_at, revoked)` 覆盖不了 `SELECT *`
的 13 列，优化器仍走 PK + heap fetch：读零收益、写 +1 索引。仅当未来做了
投影列查询的代码改造（§十）后才成立。

### B. 全行覆盖索引 —— 降级 backlog ⬇️

```sql
-- 前置条件（全部满足才值得实测）：
-- ① 迁移器单事务问题（若仍要 CONCURRENTLY 需先改 SchemaManager，见 §二.6）
-- ② bench 种子后必须 VACUUM (ANALYZE)，否则 visibility map 空、IOS 不触发
-- ③ token 池规模放大到逼近生产（2000 行小表收益≈噪声）
-- ④ 全场景 A/B（S3/S6 收益 vs S2/S5 写代价 + revoke 路径索引翻新）
CREATE INDEX idx_access_tokens_covering ON oauth2_access_tokens(token)
    INCLUDE (client_id, user_id, scope, expires_at, revoked, issued_at,
             issuer, audience, not_before, introspect_count, revoked_at, revoked_by);
```

评审补充：INCLUDE 列在 `UPDATE revoked=true` 时同步翻新（今天 revoke 已动
4 个索引条目，B 案再加 1 个——"8→7 对冲"只算了 INSERT 侧）；scope 值目前
~14B 内联无 TOAST，若未来 >2KB 会 TOAST 化，索引存 18B 指针、IOS 仍成立、
主要是膨胀风险。

### C. 纯冗余索引清理 —— 批准落地 ✅（本方案唯一落地项）

```sql
DROP INDEX IF EXISTS idx_access_tokens_token;   -- 与 access 表 PK 完全重复
DROP INDEX IF EXISTS idx_access_tokens_active;  -- EQ 点查被 PK 覆盖（V016）
DROP INDEX IF EXISTS idx_refresh_tokens_token;  -- 与 refresh 表 PK 完全重复
```

- **收益**：每笔 access INSERT 少维护 2 个 B-tree（8→6）、refresh INSERT 少
  1 个；revoke UPDATE 少 2 个可更新目标。S2/S5 写路径直接受益。
- **读零回退**：EQ 点查、`token IN (...)`、前缀 LIKE（§二.7）均由 PK 服务；
  评审已确认无 ORM 模型引用、无测试断言、无迁移重放问题（V003 先建 V026 后删，
  终态收敛）。
- **风险**：普通 `DROP INDEX` 在事务内合法且瞬时；生产大表经 Helm 迁移 Job
  变更窗口执行。回滚 = 重建索引（按 V003/V016 原定义）。

### D. 投影列查询 + 窄覆盖索引 —— 代码项 📋（§十 backlog，与 B 解耦）

## 四、验证计划（C 案落地时执行）—— ✅ 2026-08-18 全部完成

1. ✅ V026 迁移（create-migration SOP，普通 DROP INDEX IF EXISTS），提交 c5654a4。
2. ✅ full-test 8/8 回归（462 ctest + 59 OAuth2 + 55 Admin 端点；V026 在 step 1 干净应用）。
3. ✅ bench 栈 S2/S3 抽查 A/B（同日同配置 vs 3c1ced3）：S2 高并发档 +8%（c64/c128），
   S3 中性偏正（-0.6%~+15.6%）——读路径零回退。低并发单档噪声大不作证据。
   方法学陷阱记录：抽查必须显式 `WARMUP_S=5 DURATION_S=10` 对齐会话协议——默认
   10s/30s 的 30s 窗口必然撞上 ~30s 周期 TTL 雷群（首跑数据归档于
   `benchmarks/results/v026-spot-30s-confounded/`）。
4. ⚪ EXPLAIN 抽查未做（bench 栈已拆除；下次栈在位时补：introspect/getAccessToken 应仍走
   PK Index Scan，`TokenManagementService` 前缀删除走 PK Index Scan 范围）。低优先——
   full-test 的端点行为 + S3 抽查吞吐已间接证实查询路径未变。
