# 深度性能优化 — 分析报告（阶段一）

> 日期：2026-08-20 · 分支 `feat/perf-deep-optimization` · 依据 `docs/performance-optimization-prompt.md`
> 基线：`benchmarks/baseline/20260819-97c9254-*`（归档约定见 `benchmarks/baseline/README.md`）
> 方法：既有结果解析（pin 97c9254 组）+ S4 同日补基线 + EXPLAIN ANALYZE + 容器 CPU 分布采样 + 静态热点扫描（代码行号证据）

## 0. 环境与口径

| | |
|---|---|
| 机器 | Docker Desktop (WSL2)，8 vCPU / 15.6 GiB；驱动 = WSL 内 wrk 4.1.0（`localhost-cross-container`） |
| 栈 | bench overlay：cache ON（client 300s / token 60s）、PG 池 64 / Redis 池 64、`auto_batch=true`、PG17 + 实例调优、V025/V026 schema |
| 阶梯口径 | 每级 5 s 预热 + 10 s 测量（会话协议，`run-authforge-session.sh`），S1-S6 全 8 级 c2→c128 |
| 已知方差 | 跨日 S3/S6 ±8-9%；A/B 判定一律同日背靠背；本机 bench 期间禁跑任何子代理/构建 |

CPU 热点说明：容器内无 perf/gdb，`perf_event_paranoid=2` —— 提示词文档预期的 perf 路径在本机被环境阻断。降级方案：负载下 docker stats 容器 CPU 分布采样（§3.4）+ 静态扫描 + EXPLAIN 三方证据交叉。

## 1. 基线解析（20260819-97c9254，pin 组）

| 场景 | 峰值 QPS（档位） | steady 容量（err<0.01%） | knee | p99 范围 | 占 S1 天花板 |
|---|---|---|---|---|---|
| S1 discovery | **102,758**（c32） | c32 | c32→c64 | 0.17–334.7 ms | 100% |
| S2 client_credentials | 14,688（c128） | c128 | c32→c64* | 1.48–60.8 ms | 14.3% |
| S3 introspect | 17,259（c64） | c64 | c16→c32 | 1.79–229.3 ms | 16.8% |
| S5 refresh_token | 2,000（c8）† | c8 | c8→c16 | 5.34–81.0 ms | 1.9%† |
| S6 userinfo | 17,621（c128） | c128 | c32→c64* | 1.08–168.2 ms | 17.1% |

\* S2/S6 的 c64 档出现非单调凹陷（S2: 12,786→11,263→14,688；S6: 15,371→13,298→17,621），凹陷档 err 抬升（0.046%/0.038%，均为 timeout）。10 s 测量窗随机覆盖 TTL 同步到期雷群（§4.1），与 30s/60s 周期尖峰的相位关系决定该档是否踩中 —— 这是"c128 反而高于 c64"的最合理解释，gcjitter 长跑时序（§4.1）为直接证据。

† **S5 的"2k 硬饱和"一半是测量预算伪影**：RT 池 `--rt-count 20000`（`setup.sh:261`）÷ 10 s 测量窗 = 2,000 QPS 恰为窗口吞吐上限，各级 `total_requests ≈ 19999` 是铁证。服务器真实容量略高于此：c8 无排队时 8/3.37ms ≈ 2.37k；c128 排队后 in-flight ≈ 2,000×24.5ms ≈ 49 ≈ DB 侧并发上限。**历史上所有"S5 优化无效"的 A/B 结论（多轮 ~1,999 封顶）均受此污染**。修复口径（rt-count ≥ 60k 或缩窗）后才可评估 S5 优化项。

### S4 同日补基线（20260820-e30b6c5，本报告新增）

| 档位 | c16 | c32 | c64 | c128 | c256 |
|---|---|---|---|---|---|
| QPS | 349 | 479 | 483 | 501 | 536 |
| p99 | 108 ms | 172 ms | 355 ms | 541 ms | 935 ms |
| err | 0.74% | 0.02% | 0.02% | 0.02% | 0.02% |

与 20260812 旧组（峰值 465 @c32，6 级无 c128）方向一致（+3~15%，跨周仅方向性）。err ~0.02% 恒定 = 全 VU 共享 (IP, client) 限流桶的已知特性（`benchmarks/README.md` Known limitations）。c256 仍未见顶（QPS 缓升、p99 陡升 → 接近 CPU 饱和区）。

## 2. 场景差距的结构性解释（静态扫描结论）

热点扫描全文结论按路径归档于 §4 瓶颈表；此处给骨架（每请求串行 RTT 账，代码级）：

| 场景 | 串行 PG RTT | 串行 Redis RTT | 其他主导成本 | 天花板判定 |
|---|---|---|---|---|
| S1 | 0 | 0 | Drogon 框架 + JSON 构造 | 框架天花板（102k） |
| S2 | **2**（validateClient SELECT + token INSERT） | 1（client GET） | — | RTT-bound |
| S3 | **2**（validateClient SELECT + token SELECT *） | 2（EXISTS revoked + GET intro，永不命中 §4.3） | — | RTT-bound |
| S5 | **5**（UPDATE RETURNING + BEGIN/INSERT/INSERT/COMMIT 事务 + audit INSERT） | 1 | RS256 id_token 签名 | 写链 + 预算双限 |
| S6 | **3**（roles×2 串行 + user SELECT） | 2（EXISTS + GET access） | — | RTT-bound |
| S4 | **~11-12**（login 3-4 + token 交换 8） | 0 | **PBKDF2 310k 迭代/登录** | CPU（PBKDF2）+ RTT |

要点：**JwkManager 无嫌疑**（init 一次加载、无每请求 IO/重解析）；**JSON 构造无嫌疑**（S1 每请求重建 40 字段 discovery 文档仍达 102k）；**日志在 WARN 下近乎免费**（dev 的 DEBUG 配置除外——环境项）。

## 3. DB 与 CPU 证据

### 3.1 EXPLAIN ANALYZE（bench 栈实测，2026-08-20）

| 查询 | 计划 | 执行时间 | Buffers |
|---|---|---|---|
| `oauth2_access_tokens` by token（S3/S6 点查） | Index Scan `oauth2_access_tokens_pkey` | **0.050 ms** | shared hit=4 |
| `oauth2_clients` by client_id（S2） | Index Scan `oauth2_clients_pkey` | **0.021 ms** | shared hit=2 |
| `users` by username（S4 login） | Index Scan `users_username_key` | **0.021 ms** | shared hit=3 |

**结论：DB 执行器不是瓶颈**（全部 <0.1ms、全缓冲命中）；V026 判断被证实（PK 覆盖点查，所删索引冗余）。瓶颈在 **RTT 次数**（§2 账目）而非单查询执行。

### 3.2 CPU 分布（S2 c32 ≈ 12.8k QPS 负载下采样）

backend ≈ 267% · postgres ≈ 266% · redis ≈ 40%（各为 8 核中的 ~2.7 核）

**双端均衡共饱和**：每请求 = backend 工作 + 多次 PG 往返，两侧同步变忙；redis 低（缓存命中路径）。S2 的扩展空间受"两侧一起烧 CPU"限制——减 RTT 同时省两侧 CPU。

## 4. 数据驱动瓶颈表（按 预估提升 × 影响场景数 排序）

| # | 瓶颈 | 证据 | 影响场景 | 预估提升 | 置信度 | 难度 | 处置 |
|---|---|---|---|---|---|---|---|
| 1 | **缓存 TTL 同步到期雷群**：均匀轮询 2000 key × 60s TTL → 30s 周期 ~800ms p99 尖峰、QPS -25%；池越大雷群越深（pool25=291ms→pool64=~800ms） | gcjitter 时序（§4.1）；S2/S6 c64 凹陷与相位吻合 | S2/S3/S6 尾延迟与吞吐稳定性 | 尖峰段 p99/中位比 ↓≥30%（gcjitter 判定） | 高（已有仪器与实测） | 低（4 处 SET 加 jitter） | **本轮 P2.1 执行** |
| 2 | **`validateClient` 透传 + 与 `getClient` 双查**：每请求多 1 次 PG SELECT；secret/salt 本已在缓存序列化字段里（`RedisCachedClientRepository.cc:36-37`），可缓存行本地比对 | `RedisCachedClientRepository.cc:232-233`；`TokenEndpointController.cc:516+549`（introspect 双查）、`:1199+1216`（token 双查） | S2、S3（各 -1 PG RTT；S2 总共才 2 个 PG RTT） | S2 +10~25%、S3 +5~15% | 中高 | 中（常量时间比较语义保持） | 建议下一轮 |
| 3 | **S5 测量预算伪影**（rt-count 20000 ÷ 10s = 2k 上限） | §1 †；`setup.sh:261`；`total_requests:19999` | S5 全部历史 A/B 结论 | 解锁 S5 优化评估（口径修正，非服务提升） | 高 | 极低 | 建议立即修口径 |
| 4 | **S5 写链 3 次独立 commit/fsync**：UPDATE（autocommit）+ saveTokenPair 事务（`PostgresTokenRepository.cc:148-224`，独占连接 4 RTT）+ audit INSERT = S2 的 3 倍 WAL 落盘；auto_batch 对事务链无效 | 代码行号 + c128 in-flight≈49 的排队证据 | S5（S4 第二步同构） | S5 +20~40%（口径修复后验证） | 中 | 高（CTE 合并/事务语义） | 积压（§十） |
| 5 | **S6 用户/角色 3 串行 PG RTT 无缓存**：`getRoles(subject)` 两连查 + `getUserInfo` 第三查 | `TokenEndpointController.cc:1989`；`PostgresIdentityRepository.cc:523-537`；`OAuth2Plugin.cc:803` | S6 | +10~20% | 中 | 中 | 积压 |
| 6 | **introspect N2 判别器**：预植/冷 token 的 introspect 正缓存永不回填，每请求打 PG | `RedisCachedTokenRepository.cc:316-351`；bench 2000 AT 全 miss | S3 | 视放宽语义而定 | 中 | 中（撤销语义决策） | 积压（§十 已录） |
| 7 | **S4 PBKDF2 310k 迭代/登录**（30-100ms 纯 CPU）：安全参数（OWASP 2023 PBKDF2-SHA256 推荐 600k），**非缺陷** | `AuthService.cc:19,119-121` | S4 | 迭代↓=线性提升，但安全语义让步 | 高 | — | 不做（文档记录） |
| 8 | **编译档位缺失（LTO/PGO 全未设）**：IO 密集型典型增益 0-3%，可能低于噪声 floor | 构建配置核查（无 LTO/PGO 设置） | S1-S6（S1 纯 CPU 最可能显） | 0-3% | 低 | 低（opt-in 档位） | **本轮 P2.2 执行**（先 LTO 后评估 PGO） |

排序依据：#1 有直接实测证据且仪器就绪；#2 是"每请求账目"里最大的确定性浪费（S2 相对收益最大）；#3 修正后才能解锁 #4 的评估；#5/#6 单场景收益；#7 是安全权衡非优化项；#8 是提示词文档指定维度、预期管理已明确。

### 4.1 TTL 雷群时序（历史 gcjitter 证据，P2.1 的判定基线）

97c9254 组 gcjitter（auto_batch=true）：中位 p99 4.1ms、极值 380ms、雷群段 10/30；更早观测（快赢轮）：30s 周期 ~800ms p99 尖峰、QPS 谷 -25%。P2.1 验收 = 同日背靠背 gcjitter 前后对比，尖峰段 p99/中位比下降 ≥30%，S3/S6 阶梯为次证据（10s 窗随机覆盖尖峰，仅参考）。

## 5. 已排除的假说（避免重复排查）

- **JwkManager 每请求密钥重载/PEM 重解析**：不存在（`JwkManager.cc:31-121` init 一次加载）。
- **控制器 JSON 大对象拷贝**：S1 每请求重建 discovery 文档仍 102k，非瓶颈。
- **日志开销**：WARN 级别宏短路；仅 dev 配置 DEBUG 需注意（`PostgresClientRepository.cc` 单函数 7 条 LOG_DEBUG）。
- **S5 家族行锁竞争**：bench 每 RT 独立 family，`UPDATE WHERE token=$1` 无行竞争（`gen-tokens.py:101`）。
- **DB 执行器/索引**：§3.1 全部 <0.1ms 点查。
- **RateLimiter 单 mutex**：当前量级 4k 锁/s 无感（`RateLimiter.h:97,145`），S1 化吞吐才需分片。

## 6. 待验证附录（无直接量化证据，不进瓶颈表主序）

- audit 下关键路径 A/B（临时注释 `TokenService.cc:512` / `SessionController.cc:659` 跑 S5/S4）——扫描推断"中"贡献，未测。
- introspect 负缓存 2 串行 Redis RTT 合并为 pipeline/Lua（`RedisCachedTokenRepository.cc:279-376`）。
- `getParameters()` 整表拷贝（`TokenEndpointController.cc:938`）、Metrics label 先构造（`:1048-1053`）——µs 级，仅在高天花板场景值得清理。
- is_fast db/redis client（§十 deferred：config+代码双改，Redis 连接语义变化）。
- L1 CachedClientRepository 接线（失效语义未设计，见计划"明确不做"）。

## 7. 结论与执行顺序

1. **P2.1 TTL jitter**（瓶颈 #1）——工具、证据、方案齐备，本轮执行。
2. **S5 口径修复**（#3）——一行参数 + 重跑，随后 S5 优化项才可评估（本轮若时间富余顺带）。
3. **P2.2 LTO opt-in 档位**（#8）——预期 0-3%，构建+测试为硬门槛。
4. **validateClient 缓存化**（#2）——下一轮首选（S2 相对收益最大、语义保持可控）。
