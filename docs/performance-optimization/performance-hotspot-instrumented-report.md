# 性能热点工具化分析报告（instrumented analysis）

> 日期：2026-08-20 晚 · 分支 `feat/competitor-benchmark` @ cf9b6fe · 分析对象 = 最终优化后二进制（`final` 镜像 d6e851177e77：LTO + reuse_port + PG17/池 64/cache/auto_batch）
> 性质：**只分析、只报告，未实施任何优化**（用户指令）。分析用的临时插桩已全部还原，未入库。
> 前置：`performance-optimization-report.md`（静态分析版）——本报告用仪器数据验证/修正其结论。

## 0. 方法与仪器

| 阶段 | 仪器 | 窗口对齐方法 |
|---|---|---|
| A. 每请求 SQL 账本 | `pg_stat_statements`（PG17 内核级统计） | 每场景：预热 5s（丢弃）→ `pg_stat_statements_reset()` → 正式负载 10s → 快照。统计窗口严格对齐测量窗口，`calls/请求数` 即每请求语句数 |
| B. 每阶段延迟分解 | 进程内插桩（`StageProbe`：100µs 线性分桶直方图，每 10k 样本汇总一行；脏树构建，已还原） | 插桩构建（同 LTO 档）+ S6 c64 负载 15s，330k 样本 |
| C. CPU 栈采样 | gdb sidecar（`--pid=container --cap-add=SYS_PTRACE`） | **失败，见 §3** |

负载全部由 WSL 内 wrk 4.1.0 驱动（与正式基线同驱动、同 5s/10s 口径）。端到端延迟取 wrk `Latency` 均值。

## 1. Phase A — 每请求 SQL 账本（pg_stat_statements）

### 1.1 账本（c64 × 10s，全部 calls/请求 ≈ 1.000，即无隐藏查询）

| 场景 | 端到端均值 | 语句清单（每请求执行次数 × mean_exec_ms） | 执行器合计 | 占端到端 |
|---|---|---|---|---|
| S6 userinfo | 3.59 ms | `users` 点查 1×0.020 + `user_roles` 1×0.018 + `roles`(IN) 1×0.016 | **0.054 ms** | 1.5% |
| S3 introspect | 3.39 ms | `oauth2_access_tokens` 点查 1×0.020 + `oauth2_clients` 点查 1×0.016 | **0.036 ms** | 1.1% |
| S2 client_credentials | 6.05 ms | `oauth2_access_tokens` INSERT 1×0.219 + `oauth2_clients` 点查 1×0.048 | **0.267 ms** | 4.4% |
| S5 refresh_token | 12.38 ms | AT INSERT 1×0.123 + RT UPDATE(CAS) 1×0.085 + RT INSERT 1×0.084 + `audit_logs` INSERT 1×0.064 + begin/commit 2×~0 | **0.356 ms** | 2.9% |

关键读数：
- **静态分析的每请求数全部被证实**（S6=3、S3=2、S2=2、S5=6 条），且无额外隐藏查询（`calls>50` 过滤后无其它语句）。
- **DB 执行器在所有场景只占端到端延迟的 1-4%** —— 索引/执行计划在当前规模已无优化空间（与 EXPLAIN 结论一致）。
- S3/S2 的 `oauth2_clients` **每请求点查**坐实了 validateClient 透传 + getClient 双查（静态发现 #2）。
- S5 的 begin/commit mean≈0（组提交效应），写放大主项是三条 INSERT/UPDATE 本身的往返。

### 1.2 顺带澄清：S5 reseed 语义（解释终测有效性）

`run-scenario.sh:282-291` 的 reseed 实为 **`TRUNCATE oauth2_refresh_tokens` + 重放全量 INSERT**（非 `ON CONFLICT DO NOTHING` 的裸重放）—— 池每级完全重建，终测 S5（4,777 QPS）为真实成功路径吞吐，此前疑虑排除。

## 2. Phase B — S6 端到端延迟分解（插桩，330k 样本）

负载：S6 c64 × 15s，插桩构建（同 LTO 档），16.9k QPS，wrk 端到端均值 **3.88 ms**。

| 阶段 | mean | p50 | p90 | p99 | 内容 |
|---|---|---|---|---|---|
| redis_exists_rtt | 475µs | 300µs | 1000µs | 2000µs | EXISTS revoked 单次往返 |
| redis_get_rtt | 446µs | 300µs | 900µs | 1900µs | GET access 单次往返 |
| **filter_validate** | **964µs** | 700µs | 1800µs | 3300µs | = exists+get+调度残余 ~43µs ✓ |
| **roles**（2 次串行 PG） | **1416µs** | 1200µs | 2500µs | 4200µs | 执行器仅 2×20µs |
| **profile**（1 次 PG） | **667µs** | 500µs | 1300µs | 2600µs | 执行器仅 20µs |
| **handler_total** | **2089µs** | 1900µs | 3500µs | 5400µs | = roles+profile+~6µs ✓ |

端到端 3.88ms 的完整去向：

| 成分 | 耗时 | 占比 |
|---|---|---|
| token 校验（Redis ×2 串行往返） | 0.96 ms | 25% |
| roles 查询（PG ×2 串行往返） | 1.42 ms | 37% |
| profile 查询（PG ×1 往返） | 0.67 ms | 17% |
| 框架/HTTP/JSON/调度残差（3.88−0.96−2.09） | 0.83 ms | 21% |

两处交叉验证自洽（filter=两次 Redis 往返之和；handler=两段查询之和），数据可信。

**核心推论 —— 单次往返的真实价格**：
- PG 单往返 ≈ **670µs**（profile 667µs；roles 1416/2=708µs），其中 SQL 执行 20µs → **一次 PG 往返的 97% 是传输+事件循环调度，不是查询本身**
- Redis 单往返 ≈ **450µs**
- "串行往返次数主导延迟"从静态假设变为仪器事实：S6 五次串行往返（Redis 2 + PG 3）占端到端 **79%**

## 3. Phase C — CPU 栈采样（gdb poor-man's profiler，最终成功；perf 被内核阻断）

### 3.1 perf 路线：stock WSL2 内核不可用（证据归档）

`perf_event_paranoid` 已通过 WSL sysctl 打开（值 2→1，全局内核 sysctl，容器内可见性已验证），但三重诊断证明采样不可用：
- 自采样（`perf record -e cpu-clock -- sleep 2`）：**2 秒仅 1 个样本**；
- `perf stat -p 1`：cycles/instructions **`<not supported>`**（无硬件 PMU）；
- 系统级 `perf record -a -F 199`（8s）：12,736 样本几乎全部为内核 idle（`pv_native_safe_halt`），负载中的 authforge-server/wrk **用户态样本为零**。

官方解法（Microsoft Learn / 内核文档）：`.wslconfig [wsl2] kernelCommandLine = perf_event_paranoid=1` 仅解除权限位，采样能力缺失需**自编译带 perf 支持的 WSL2 内核** —— 改整机内核超出分析任务范畴，记为 follow-up。

### 3.2 gdb 路线：三关全破，最终拿到函数级样本

| 关卡 | 症状 | 解法 |
|---|---|---|
| 符号化 | 所有帧 `??` | ① 构建加 `-fno-omit-frame-pointer -g`（脏 preset，已还原）；② **`gdb -ex 'set sysroot /'`** —— 关键钥匙：跨容器 attach 时 gdb 默认经 `target:` 前缀读目标二进制被 EPERM 拒绝（warning 可见），`set sysroot /` 强制读 sidecar 本地文件系统 |
| 采样对象 | 全部样本 idle | 容器内 **PID 1 是监督进程（2 线程，`wait4` 等子进程），真服务器是 fork 出的 PID 8（26 线程）** —— 必须采子进程 |
| 负载挂载 | 负载没跑 | `(wrk &)` 在 wsl.exe 退出即死 —— 负载必须作为 harness 级后台任务（或 `nohup setsid`） |

sidecar 配方：`docker run --rm --pid=container:oauth2-backend --cap-add=SYS_PTRACE <server-image> bash -c "apt-get install gdb && gdb -batch -ex 'set sysroot /' -ex 'file /app/authforge-server' -ex 'attach 8' -ex 'thread apply all bt 20'"`（sidecar 必须用服务器镜像自身 —— 符号文件路径才与 /proc/PID/exe 对齐）。

### 3.3 采样结果（35 轮 × 26 线程，S6 c64 负载下，520+ 叶子帧）

| 叶子帧 | 样本数 | 占比 | 解读 |
|---|---|---|---|
| `epoll_wait` | 385 | **74%** | 26 线程在 8 vCPU 上，多数 IO 线程空闲（c64 未饱和线程池） |
| `__libc_send` / `__libc_write` / `recv` | ~49 | **9%** | 响应/socket 写出；其中 6 帧在 **`pqsecure_raw_write`/`pqSendSome`（libpq 线缆写）** —— PG 往返在线程栈上的直接可见证据 |
| `__tz_convert` / `__tzfile_read`（glibc tz 锁） | ~20 | **4%** | **热路径存在 localtime 类调用，每调用拿 glibc 全局 tz 锁**；一方代码 grep 零命中（排除），来自 trantor/drogon/libpq 层；精确调用点被 LTO 内联吞掉（栈归因到 EpollPoller::poll/handleEventSafely）。可用非 LTO 构建或 ltrace 解析；缓解假设：TZ=UTC 实验 |
| malloc arena / rand 锁（futex） | ~8 | ~1.5% | glibc 分配器竞争，量级可忽略 |
| 纯 CPU 计算（memcpy/malloc/RB-tree/…） | <10 | **<2%** | 与 Phase B 结论一致：CPU 计算不是瓶颈 |

**Phase C 结论**：CPU 侧确认无显著热点（计算 <2%）；新发现的唯一可行动小项是 **glibc tz 锁竞争（~4%）**；libpq 线缆写在栈上直接可见，与 Phase A/B 的"往返主导"互证。采样扰动说明：attach 风暴会使吞吐从 ~16.9k 降到 ~14.5k QPS（样本期数据只用于归因，不用于吞吐）。

## 4. 修正后的瓶颈排序与量化杠杆（**均未实施**）

| # | 杠杆 | 仪器依据 | 预估收益（同法 A/B 验证后为准） | 影响场景 |
|---|---|---|---|---|
| 1 | **S6 用户/角色缓存化**（消 3 次 PG 往返） | §2：3×~670µs = 2.09ms/请求（占 54%） | 均值 3.88→~1.8ms，理论吞吐 ~1.9x（18k→33k+ 量级，正好压 Keycloak 线） | S6 |
| 2 | **validateClient 缓存行/复用 getClient 结果**（消每请求 1 次 PG 往返+双查） | §1.1：S2/S3 各 1×clients 点查（~670µs RTT + 20-48µs 执行） | S2 ~+10-15%、S3 ~+20%（S3 仅 2 次 PG 往返，消 1 次即半） | S2、S3 |
| 3 | **S6 roles/profile 并行化**（两链独立，现为串行嵌套） | §2：handler=2.09ms → max(1.42,0.67)≈1.45ms | 省 ~0.65ms/请求（与 #1 叠加时收益并入 #1） | S6 |
| 4 | **Redis 双往返合并**（EXISTS+GET → pipeline/Lua 一次） | §2：2×450µs → ~1×450µs | 省 ~0.45ms/请求 | S6、S3 |
| 5 | S5 写链合并（UPDATE+事务+audit 6 语句 → 更少往返/提交） | §1.1：6 语句串行，12.38ms 端到端 | 需语义设计（CTE 合并/事务边界），量级待 A/B | S5、S4 第二步 |
| 6 | **glibc tz 锁消除**（热路径 localtime 类调用 → 缓存格式化/TZ=UTC） | §3.3：~4% 线程样本阻塞在 `__tz_convert`；三方库引入，一方代码已排除 | 单独看 ~3-4%；可与 #1-#4 合并轮次 | 全场景 |
| — | DB 执行器/索引 | §1.1：占 1-4% | **零收益，排除**（V026 后索引已最优） | — |
| — | 框架残差 0.83ms | §2 | 21%，属 Drogon 本体（JSON 构造在 S1 已证非瓶颈），非应用层可优化 | — |

与静态版报告的差异：静态版瓶颈表 #5（S6 缓存）预估 +10~20% —— 仪器数据支持更大幅度（~1.9x 理论），因为 3 次往返实际占 54% 而非此前推断的占比；静态版"执行器不是瓶颈"从推断变为实测。

## 5. Follow-up 清单

1. ~~帧指针+调试信息的专用剖析构建（火焰图）~~ **已完成（§3.2 配方，gdb 路线）**；perf 火焰图仍需自编译 WSL2 内核（§3.1），仅在 gdb 采样不够时考虑。
2. 上述杠杆 #1-#6 的逐项实施 + 同日 A/B（规则 3）——**待用户批准后执行，本报告未动代码**。
3. tz 锁精确调用点解析（非 LTO 构建或 ltrace）——若实施 #6 时需要。
4. 四产品对比重跑（竞品侧仍是优化前口径）。
5. `pg_stat_statements` 可考虑常驻 bench 栈（ALTER SYSTEM 已验证可用、开销可忽略），作为未来 A/B 的常规证据源。

## 附录：插桩复现要点（未入库）

脏树改动 4 文件（分析后已还原）：`libs/drogon/src/StageProbe.h`（新文件：atomic 计数 + 100µs 线性分桶 201 桶 + 每 10k 样本 LOG_WARN 汇总，`steady_clock` 计时）；`OAuth2AuthFilter.cc`（userinfo 路径 entry→validateAccessToken 回调）；`TokenEndpointController.cc`（userInfo 的 roles/profile/total 三段）；`RedisCachedTokenRepository.cc`（getAccessToken 的 EXISTS/GET 两段）。跨库相对路径 include（`../../drogon/src/StageProbe.h`），namespace 需全限定 `::authforge::drogon::stageprobe`。
