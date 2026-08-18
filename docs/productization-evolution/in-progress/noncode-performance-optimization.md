# AuthForge 非代码层性能优化方案

> **版本**: v1.0（2026-08-18）
> **日期**: 2026-08-18
> **文档性质**: 优化方案与分析（仅非代码层：配置、依赖选型与版本、DB 实例与 schema、运行环境、构建、基准 SOP）
> **数据基线**: 2026-08-17 四产品同 session 实测（`benchmarks/competitors/results/COMPARISON.md`）+ Phase 0 自测（`benchmarks/results/SUMMARY.md`）
> **范围声明**: 不含 C++ 应用代码改动。需要配套代码的项单列 §十（`is_fast` 即此类——开启需改造数据访问代码，不计入本方案收益预估）。

---

## 零、TL;DR

按预期收益排序的纯配置/部署层杠杆，**前四项合计有望 S2/S5 提升 1.5–2.5 倍、S3/S6 提升 2–4 倍、消除秒级尾延迟尖峰**（均为预估值，须 A/B 实测裁决）：

| # | 杠杆 | 层面 | 预估收益 | 成本/风险 |
|---|---|---|---|---|
| 1 | 开启 Redis L2 缓存（`cache.enabled=true`） | 产品配置 | S3/S6 **2–4x**；S2 +30–60% | 极低：能力已实现（#42 Phase 1/2），Redis 已在栈内 |
| 2 | PG 实例调优（checkpoint/WAL/shared_buffers） | DB 配置 | S2/S5 +20–50%；消灭 655ms 级尖峰 | 极低 |
| 3 | 审计写降级（UNLOGGED/BRIN/分区） | schema | S2/S5 +15–30% | 中：合规语义决策 |
| 4 | PG 15 → 17 | 组件版本 | 写路径 +10–25%；消除 client 17.7/server 15 错位 | 低 |
| 5 | 连接池扫描（25→64/100） | 产品配置 | 高并发档 P99 大幅下降 | 低 |
| 6 | host network + `reuse_port` | 环境/配置 | S1 +10–20% | 低 |
| 7 | `synchronous_commit=off`（吞吐档） | DB 配置 | S2/S5 写延迟 -30–50% | 中：崩溃丢 ≤0.5s 提交 |
| 8 | LTO/PGO/`-march=native` | 构建层 | 全场景 +5–15% | 低 |
| 9 | 驱动分离 / 裸机 / `.wslconfig` | 环境 | 绝对值 +30–100% | 需硬件 |

---

## 一、瓶颈归因（从实测数据反推）

| 场景 | 稳态 QPS | 每请求真实工作 | 瓶颈判断 |
|---|---|---|---|
| S1 discovery | 94,640 (c=64) | 纯框架路径 | 8 vCPU 上 Drogon + veth 网络栈天花板；driver CPU 仅 17%。c=128 反降 + P99 403ms = accept/连接排队 |
| S2 client_credentials | 9,056 (c=32) | client SELECT + **token INSERT + audit INSERT** | **PG 写路径**：2 行写入、维护 token 表 3 索引 + audit 表 4 索引 |
| S3 introspect | 17,602 (c=64) | token SELECT（哈希点查，opaque token） | PG 读路径（无审计写，故为 S2 的 2 倍） |
| S5 refresh | 1,998 (c=16) | 旧 RT 读+吊销 + 新对写入 + 审计 + 家族检查 | PG 多写事务链，写放大最重 |
| S6 userinfo | 18,278 (c=32) | token SELECT + user SELECT | PG 读路径 |

两个铁证：
- **GC 抖动曲线的周期性尖峰**（655ms/1480ms，30 段中 7 段 >1.5x 中位）——S6 纯读、服务器无写，尖峰只能来自 **PG checkpoint full-page-write 风暴 + WSL2 IO**。PG 出厂默认（checkpoint_timeout=5min、max_wal_size=1GB、shared_buffers=128MB）在 S2 写入的 WAL 积累下必然周期刷盘。
- **高并发 P99 退化**（S1 c≥64 达 107–403ms）是连接池排队（Phase 0 已裁决）；池 25 是四产品对齐值，非 AuthForge 最优。

---

## 二、Drogon 配置文件全字段审计（config.bench.json）

> 逐字段核对 Drogon 官方配置文档（wiki ENG-11 Configuration File / ENG-08-4 FastDbClient / ENG-08-5 auto_batch / ENG-18 Redis）。
> 结论标记：✅ 已最优 / ⚪ 无热路径影响 / 🔧 可优化 / ⚠️ 风险 / 📋 待办（需代码，本方案不计收益）。

### 2.1 `listeners`（4 字段）

| 字段 | 现值 | 结论 |
|---|---|---|
| address / port / https | 0.0.0.0 / 5555 / false | ✅ HTTP 直听为最快路径；基准不开 TLS（竞品中仅 Ory 因 v26 强制 https 开了自签，已在对比中标注） |

### 2.2 `db_clients`（10 字段）

| 字段 | 现值 | 结论 |
|---|---|---|
| name/rdbms/host/port/dbname/user/passwd | default/postgresql/… | ⚪ 连接参数 |
| `is_fast` | **false** | 📋 待办：fast 客户端共享 IO 线程、消除跨线程通信（官方确认更快），但**禁止同步接口调用**、连接数语义变化（见 2.3 陷阱）——现有 Mapper/异步回调数据访问代码需改造后才能启用，**不计入本方案** |
| `client_encoding` | "" | ⚪ |
| `number_of_connections` | 25 | 🔧 池扫描（§五）：高并发 P99 尾巴即池排队；产品自身容量口径应另测 64/100 |
| `timeout` | -1.0 | ✅ 命令执行超时关闭（真正的护栏是服务端 statement_timeout） |
| `auto_batch` | **true** | ⚠️ **违反官方使用建议**：auto_batch 仅建议用于只读/非关键查询的独立客户端；当前唯一 default 客户端**同时承载 token/audit 写入**——批内单条 SQL 失败会波及整批、回滚通知可能丢失。缓解二选一：(a) 写路径拆独立客户端（`auto_batch=false`）+ 读路径专用 batch 客户端（需 repository 接线改造，📋 半代码项）；(b) 保守置 false（微损失、零风险）。**至少应在文档记录该风险并拍板** |
| `connect_options.statement_timeout` | 5s | ✅ 服务端护栏，保留 |

### 2.3 `redis_clients`（8 字段）

| 字段 | 现值 | 结论 |
|---|---|---|
| name/host/port/username/passwd/db | … | ⚪ |
| `is_fast` | false | 📋 同 db_clients；**陷阱记录**：`is_fast=true` 时 `number_of_connections` 语义变为**每 IO 线程**的连接数（现 20 → 9 线程下实际 180 连接），启用前必须同步改数值 |
| `number_of_connections` | 20 | 🔧 开启 cache 后读负载转移到 Redis，重估（32/64 扫描） |
| `timeout` | -1.0 | ✅ |

### 2.4 `app`（43 字段）

| 字段 | 现值 | 结论 |
|---|---|---|
| `number_of_threads` | 0（=核数） | 🔧 同机压测场景可试 6–7（与 wrk 争核）；独立驱动/裸机保持 0 |
| `enable_session` | true | 🔧 API-only 部署可关（OAuth2 语义不依赖服务端会话）——每请求开销待 A/B 验证 |
| session_timeout / session_same_site / session_cookie_key / session_max_age | 3600/Lax/JSESSIONID/3600 | ⚪ 随 session 开关联动 |
| document_root / home_page / use_implicit_page / implicit_page | "./" / index.html / false / … | 🔧 API-only 部署可清空（消除未命中路由的文件系统探测；已注册端点不受影响） |
| static_file_headers / upload_path / file_types / mime / locations | … | ⚪ 静态文件，无热路径影响 |
| `max_connections` | 100000 | ✅ |
| `max_connections_per_ip` | 0 | ✅ |
| load_dynamic_views / dynamic_views_path / dynamic_views_output_path | false/… | ✅ |
| json_parser_stack_limit | 1000 | ⚪ |
| `enable_unicode_escaping_in_json` | **false** | ✅ 已是最优（true 会显著拖慢 JSON 序列化） |
| float_precision_in_json | 0/significant | ⚪ |
| log.use_spdlog / log_path / log_size_limit | true / ./logs / 100MB | ✅ |
| log.`max_files` | **0（无限）** | 🔧 磁盘治理项：设上限（如 10），非性能 |
| log.`log_level` | **WARN** | ✅ 基准/生产已是低噪档；性能问题不在等级而在 audit 结构性写库（§四.3） |
| log.display_local_time | true | ⚪ WARN 级无热度，无所谓 |
| run_as_daemon / handle_sig_term / relaunch_on_error | false/true/true | ✅ |
| `use_sendfile` | true | ✅ |
| `use_gzip` / `use_brotli` | true / true | 🔧 API-only 可关：wrk 无 Accept-Encoding 时零成本；真实客户端带压缩头时小 JSON（<1KB）压缩纯属 CPU 浪费——防御性关闭 |
| static_files_cache_time / gzip_static / br_static | 86400/true/true | ⚪ |
| simple_controllers_map | [] | ✅ |
| `idle_connection_timeout` | 60 | ✅ |
| `enable_server_header` / `server_header_field` | true / OAuth2Server | 🔧 微优化：bench 档可关（每响应省一次头构造） |
| `enable_date_header` | true | 🔧 微优化：同上（每响应日期格式化） |
| `keepalive_requests` | 10000 | ✅ |
| `pipelining_requests` | 4096 | ✅（wrk 不流水线，无成本） |
| client_max_body_size / client_max_memory_body_size | 20M / 64K | ✅ |
| `reuse_port` | **false** | 🔧 建议 A/B true（SO_REUSEPORT 多 listener 分摊 accept，S1 c=128 排队场景受益） |
| `enabled_compressed_request` | true | ⚪ 仅请求带 Content-Encoding 才有成本 |
| `enable_request_stream` | false | ✅ |

### 2.5 `plugins` / `custom_config`

| 项 | 现值 | 结论 |
|---|---|---|
| PromExporter | 加载 | 🔧 基准档可去掉（每请求指标聚合固定成本）；生产保留 |
| OAuth2Plugin.`cache.enabled` | **false** | 🔧 **头号杠杆**：client 缓存（TTL 300s）+ token 缓存（getAccessToken/introspectToken，TTL 60s，含 negative cache 与 revoke 失效）——S3/S6 读路径离开 PG，S2 省 client 查询 |
| OAuth2Plugin.cleanup_interval_seconds | 3600 | ✅（配合 §四.4 分区后由 DELETE 变 DROP） |
| OAuth2Plugin.tokens.*_ttl | 3600/2592000/600 | ⚪ 语义项，不动 |
| custom_config.auth.rate_limit | 30 次/60s | ⚪ 登录失败路径，不在 S2 热点 |

---

## 三、依赖组件选型与版本

| 组件 | 现状 | 分析 |
|---|---|---|
| PostgreSQL | 服务端 **15**-alpine，客户端 libpq **17.7** | **版本错位**。升 17：写路径实质改进（vacuum 内存、WAL 体积、写放大下降）+ 消除错位。对比公平性：D1"同 PG 版本"把四家一起升即可 |
| Redis | 7-alpine | 当前线 ✓。**Valkey 8** 为合法备选：社区活跃、无 RSAL/SSPL 商业分发顾虑（Redis 7.4+ 许可变更）、性能相当或略优；开 cache 后顺带评估 |
| Drogon / Trantor | 1.9.13 / 1.5.26 | 近期版本；跟踪上游 release note 的 IO 线程优化即可 |
| OpenSSL | 3.5.7 | 新 ✓；若未来 JWT 档上线，ECDSA/EdDSA 签名性能依赖此层（§十） |
| hiredis | 1.2.0 | 够用；cache 开启后若 Redis 成热点再升 |

---

## 四、PostgreSQL 实例调优（单项收益最大的部署改动）

现状：compose **零调优**（出厂默认 shared_buffers=128MB，仅为 16GB 的 0.8%）。新增 `postgresql.conf`（compose `command` 挂载）：

```conf
# 内存
shared_buffers = 4GB
effective_cache_size = 12GB
work_mem = 16MB

# WAL / checkpoint —— 直接针对 655ms 尖峰
checkpoint_timeout = 15min       # 默认 5min，checkpoint 频率降 3 倍
max_wal_size = 4GB               # 默认 1GB
min_wal_size = 1GB
wal_compression = on             # FPI 体积减半以上
checkpoint_completion_target = 0.9

# 高频写入 token 表
autovacuum_vacuum_insert_scale_factor = 0.02
autovacuum_vacuum_scale_factor = 0.02

# 吞吐档决策项（见 §八）
# synchronous_commit = off
```

### schema 层（migration 级，非应用代码）

1. **audit_logs 降级**（三选一，保守→激进）：UNLOGGED（写快 2–3 倍，崩溃丢审计——合规档不可） / BRIN 替代 B-tree（纯 append 表维护成本近零） / 时间分区+定期 DROP。当前每笔发放/刷新都写 audit 行（fire-and-forget 但占池连接 + 维护 4 索引），S2 写放大直接翻倍。
2. **oauth2_access_tokens 真 RANGE 分区**（按 expires_at 月度）：V016 名为 partitioning_prep 实际只有索引+归档函数；分区让 cleanup 从 DELETE 变 DROP、索引深度受控。
3. **introspect 覆盖索引**：`ON oauth2_access_tokens(token) INCLUDE (user_id, client_id, scope, expires_at, revoked)` → index-only scan。

---

## 五、连接池与运行环境

1. **池扫描**：25（对齐值）→ 64/100 扫描，作为产品容量规划数据；430ms P99 尾巴即 25 打满的池排队。
2. **host network**：现 wrk → docker-proxy → veth → 容器每包两次栈穿越；`network_mode: host` 直收（四家同享，公平保持）。
3. **WSL2**：`.wslconfig` 确保 8 vCPU 足额、关 Windows 后台；IO 是 WSL2 短板——**对外发布数字应在裸机/Linux 原生重测**（设计 R7 已预留）。
4. **驱动分离**：正式数字用第二台机器压 wrk（cache 命中后 driver 会成新瓶颈）。
5. **构建层**：LTO（`CMAKE_INTERPROCEDURAL_OPTIMIZATION=ON`）+ 自托管档 `-march=native`；进阶 PGO（benchmark 流量做 profile）。

---

## 六、基准 SOP 逻辑改进

1. **cache 应该开**：现口径把 AuthForge 按"裸 DB"测，而 Keycloak S6 优势恰来自 JWT 离线校验——cache on 才是同语义对比。主表用官方推荐配置（cache on），附录保留 cache off 消融数据。
2. **池=25 是公平约束不是性能结论**：补 AuthForge 池扫描轮。
3. **单次 10s → 每档 3 次取中位**：R7 说了"复跑取中位"但 runner 没实现。
4. **GC 抖动实验补 PG 侧证据**：同步采集 `pg_stat_bgwriter`/`pg_stat_checkpointer`，把尖峰归因坐实（checkpoint vs WSL 调度）。
5. **PG 配置快照入 JSON**：env 块记录 PG 版本 + 关键 GUC，保证可复现。

---

## 七、公平性口径

原则：**写进官方部署文档的配置 = 官方配置；没写的 = 调优**。
- cache on / PG conf / host network 先落 `deploy/` 推荐配置与 docs，再作为基准基线；COMPARISON 附录照实标注。
- 竞品维持各自官方推荐配置（现状即如此）；四家统一标准即公平。
- `synchronous_commit=off` 等语义取舍项**双档发布**（默认档/吞吐档），避免单一数字背书。

---

## 八、执行计划

| 阶段 | 内容 | 验证 |
|---|---|---|
| 快赢（半天） | cache on + PG conf + host network + bench 档微优化（关 gzip/date/server header、去 PromExporter） | `run-authforge-session.sh` A/B，S2/S3/S6 直接对比 |
| 第二步（1 天） | audit 降级拍板落地 + 池扫描 + PG17 + 池/Redis 连接数联动重估 | bench + full-test 回归 |
| 第三步（2–3 天） | token 表分区 + 覆盖索引 + LTO/PGO 构建档 | bench + full-test |
| 发布前 | 裸机/独立驱动重测（对外数字）+ SOP 三改（3 次中位、PG 侧采集、配置快照） | `run-comparison.sh --fresh` 全量 |

---

## 九、每项预估收益汇总（均为待实测估计）

| 杠杆 | S1 | S2 | S3 | S5 | S6 | 尾延迟 |
|---|---|---|---|---|---|---|
| cache on | — | +30–60% | **2–4x** | — | **2–4x** | — |
| PG conf | — | +20–50% | +10–20% | +20–50% | +10–20% | **655ms 尖峰消除** |
| audit 降级 | — | +15–30% | — | +15–30% | — | — |
| PG17 | — | +10–25% | +5–10% | +10–25% | +5–10% | ↓ |
| 池 64/100 | 高档 +10% | +5–15% | +5–15% | +5% | +5–15% | **430ms→大降** |
| host net + reuse_port | +10–20% | +3–5% | +3–5% | +3% | +3–5% | ↓ |
| sync_commit=off（吞吐档） | — | +15–30% | — | +15–30% | — | — |
| LTO/PGO | +5–15% | +5–15% | +5–15% | +5–15% | +5–15% | — |

---

## 十、待办：需配套代码的项（本方案明确不计入）

| 项 | 说明 | 前置 |
|---|---|---|
| `db_clients.is_fast=true` | fast 客户端消除跨线程通信（官方确认更快），但禁止同步接口、连接数语义变化——现有 Mapper/异步回调数据访问代码需改造 | 代码审计 + 改造 |
| `redis_clients.is_fast=true` | 同上；**陷阱**：启用后 `number_of_connections` 变为每 IO 线程数（20 → 9 线程 = 180 连接），必须同步调整数值 | 同上 |
| auto_batch 读写拆分 | 按官方建议：读路径专用 batch 客户端，写路径独立 `auto_batch=false` 客户端 | repository 接线改造 |
| JWT 档 + ES256/EdDSA | 当前 opaque token 无签名热点；若提供 JWT 档，签名算法选型是新的数量级杠杆 | 产品决策 |

---

## 附录：与本方案相关的实测证据索引

- S2 写路径双 INSERT（token + audit）：`libs/oauth2/src/protocol/TokenService.cc`（audit 调用于 316/407/448/512）+ `libs/drogon/src/observability/AuditLogger.cc`（Mapper::insert 落库）
- cache 能力与开关：`libs/drogon/src/plugin/OAuth2Plugin.cc:270-340`（RedisCachedClientRepository + RedisCachedTokenRepository，默认 OFF）
- 尖峰数据：`benchmarks/competitors/results/20260817-03965fa-authforge-gcjitter.json`（30 段 P99，7 段尖峰，最大 655.5ms）
- Drogon 字段语义：官方 wiki ENG-11（配置）/ ENG-08-4（FastDbClient）/ ENG-08-5（auto_batch 使用限制）/ ENG-18（Redis is_fast 连接数语义）
