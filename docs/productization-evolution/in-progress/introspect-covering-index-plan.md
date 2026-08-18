# introspect 覆盖索引方案（待评审稿 v1）

> **日期**: 2026-08-18
> **来源**: `noncode-performance-optimization.md` §四.3 / 用户指令"introspect 覆盖索引出个方案，子代理评审下"
> **状态**: 待评审 —— 评审通过前不落地

---

## 一、目标与背景

S3 introspect（~20k QPS）与 S6 userinfo 的 token 点查是基准中最热的 PG 读路径。
cache 开启后 S3 仍每请求落库：正向 token 缓存受 **N2 判别器**约束（仅走过
发放/校验路径的 token 才回填），bench 预植 token 永不回填（见 SUMMARY.md 快赢
A/B 节裁决 2）。因此 S3 的 DB 点查优化会直接反映在吞吐上；此外 **TTL 同步到期
雷群**（30s 周期 ~800ms 尖峰）的全量回源也走同一条查询，索引收益可放大。

目标：降低 token 点查的单次成本（heap fetch → index-only scan），且不推高
S2/S5 写路径放大。

## 二、代码与 schema 事实（方案依据）

1. **查询形状**：`PostgresTokenRepository::introspectToken`
   （`libs/storage-postgres/src/PostgresTokenRepository.cc:500`）与
   `getAccessToken`（`:229`）均为 `Mapper::findOne(Criteria(token EQ))` →
   Drogon 生成 **`SELECT * FROM oauth2_access_tokens WHERE token=$1 LIMIT 1`**。
2. **introspect 是纯读**：PG 路径无 UPDATE（`introspect_count` 仅内存实现递增，
   `MemoryTokenRepository.cc:224`；PG 的 introspectToken 不回写计数）。
3. **响应实际消费 10 列**：token, revoked, expires_at, client_id, issued_at,
   issuer, audience, not_before, user_id, scope（`TokenService.cc:586` 起、
   repository 回调 520-547 行）。表共 14 列，另有 introspect_count, revoked_at,
   revoked_by 三列 introspect 不消费但 `SELECT *` 会取。
4. **表 DDL**（`V002__oauth2_core.sql:39`）：PK = `token VARCHAR(100)`（存
   SHA-256 hex，实际 64 字节），无分区。
5. **现有索引 8 个 B-tree**：PK(token) + V003 四个（`idx_access_tokens_token
   ON(token)`、client_id、expires_at、`revoked,expires_at`）+ V016 三个
   （`idx_access_tokens_active ON(token) WHERE revoked=false`、
   user_active(user_id,revoked,expires_at)、client_active(client_id,revoked,
   expires_at)）。
   **其中两个与 PK 冗余**：`idx_access_tokens_token` 与 PK 键列完全相同；
   `idx_access_tokens_active` 对 EQ 点查同样被 PK 覆盖（partial 谓词只对
   token 范围扫描有意义，全仓库 token 查询均为 EQ 点查）。
6. **migration 执行方式**：`MigrationRunner.cc` 逐文件 `execSqlSync`，无事务
   包裹 → `CREATE INDEX CONCURRENTLY` 技术上可用（生产大表不锁写）。

## 三、方案选项

### A. 文档原案（§四.3 原文）——单独不可行 ❌

```sql
CREATE INDEX ON oauth2_access_tokens(token)
    INCLUDE (user_id, client_id, scope, expires_at, revoked);
```

**问题**：查询是 `SELECT *`，IOS 要求**全部选中列**都在索引里；INCLUDE 5 列
覆盖不了 14 列中的 10 个消费列（缺 issued_at/issuer/audience/not_before 等）。
结果：优化器仍走 PK + heap fetch，**读零收益、写 +1 索引维护**。仅当未来做
了投影列查询的代码改造（§十 D 项）后此索引才成立。

### B. 全行覆盖索引 —— 可行但要实测裁决 ⚖️

```sql
CREATE INDEX CONCURRENTLY IF NOT EXISTS idx_access_tokens_covering
    ON oauth2_access_tokens(token)
    INCLUDE (client_id, user_id, scope, expires_at, revoked, issued_at,
             issuer, audience, not_before, introspect_count, revoked_at, revoked_by);
DROP INDEX CONCURRENTLY IF EXISTS idx_access_tokens_token;   -- 见 C
DROP INDEX CONCURRENTLY IF EXISTS idx_access_tokens_active;  -- 见 C
```

- **收益**：token 点查变 index-only scan（省 1 次 heap fetch/次）；删除 2 个
  冗余索引后净索引数 8→7，写放大部分对冲。
- **代价/风险**：索引近全行宽（含 TEXT scope），token 表存储近似翻倍；S2/S5
  INSERT 多维护一个宽索引；IOS 依赖 visibility map all-visible（持续 INSERT
  的页不 all-visible，混合写场景收益打折）。
- **适用判断**：读多写少（S3/S6 形态）收益、S2/S5 写形态付费——必须 A/B
  实测全场景后裁决，不能只看 S3。

### C. 纯冗余索引清理 —— 无脑正收益，推荐先行 ✅

```sql
DROP INDEX CONCURRENTLY IF EXISTS idx_access_tokens_token;   -- 与 PK 完全重复
DROP INDEX CONCURRENTLY IF EXISTS idx_access_tokens_active;  -- EQ 点查被 PK 覆盖
```

- **收益**：每笔 token INSERT 少维护 2 个 B-tree（8→6，写放大 **-25%**），
  S2/S5 直接受益；REVOKE 的 UPDATE 少 2 个可更新目标。
- **读零回退**：全仓库对 token 列只有 EQ 点查（introspect/getAccessToken/
  revoke/cleanup 逐条删），全部由 PK 唯一索引服务；两索引被删后 planner 仍选
  PK。评审时需用 EXPLAIN 清单逐条复核（见 §五）。
- oauth2_refresh_tokens 的 `idx_refresh_tokens_token` 同样与 PK 重复，可一并
  清理（同一论证）。

### D. 投影列查询 + 窄覆盖索引 —— 代码项，本方案不计入 📋

把 introspect 的 `findOne`（SELECT *）改成只取 10 个消费列（需绕过 Mapper
全列语义或扩展 repository 接口），届时方案 A 的 5 列 INCLUDE 即可达成 IOS。
属 `noncode-performance-optimization.md` §十 backlog，与 `is_fast` 同类。

## 四、推荐路线

**C 先行落地**（V026，纯 DROP，可逆、零功能影响），**B 作为可选第二档**在
C 之上 A/B 实测（S3/S6 收益 vs S2/S5 代价），数据说话后再决定去留；A 单独
不做，等 D 落地时再启用窄版。

## 五、风险清单（请评审人逐条核对）

| # | 风险 | 评估要点 |
|---|---|---|
| R1 | 被DROP索引有隐藏使用方（admin/cleanup/EXPLAIN 未覆盖的查询） | grep 全仓库 `oauth2_access_tokens` 的 SQL/Mapper 调用；`pg_stat_user_indexes.idx_scan` 实测佐证 |
| R2 | B 案写放大推高 S2/S5、TTL 雷群更深 | 全场景 A/B，不只测 S3 |
| R3 | IOS 不生效（visibility map / planner 不选） | EXPLAIN (ANALYZE, BUFFERS) 验证 `Index Only Scan` 且 heap fetches=0 增长 |
| R4 | scope TEXT 无上限导致索引膨胀 | 检查实际 scope 长度分布；必要时 B 案从 INCLUDE 剔除 scope（损失 IOS，退化为 C） |
| R5 | CONCURRENTLY 失败留 INVALID 索引 | 迁移脚本需容错（重建前先 DROP INVALID） |
| R6 | PG13（本地 dev/full_test 栈）兼容 | INCLUDE/CONCURRENTLY 均 PG11+，无兼容问题 |
| R7 | 事务性回滚 | DROP INDEX 瞬时可逆；B 案新增索引同样 DROP 即回滚，无数据迁移 |

## 六、验证计划（若评审通过）

1. V026 迁移（create-migration SOP）+ 本地 PG13 冒烟（introspect 端到端）。
2. EXPLAIN 清单：introspect/getAccessToken/refresh 查 revoke/cleanup/归档查询。
3. bench 栈全场景 A/B（C 档 vs C+B 档），对照 a9d6327。
4. full-test 8/8（与 bench 串行）。
