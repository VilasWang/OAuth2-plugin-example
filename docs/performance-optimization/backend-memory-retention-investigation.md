# Backend 内存留存调查报告（discovery 路径无界泄漏）

> 日期：2026-08-22 · 分支 `feat/competitor-benchmark` · 性质：**只调查，未修复**
> 起因：正式对比表（2026-08-21）中 AuthForge 全栈 RSS 5,350 MiB 四家最重，破案发现 backend 容器均值 4,711 MiB 才是大头 —— 本调查定位其机制。

## 1. 结论（TL;DR）

**discovery 路径存在真实的无界内存泄漏：约 730 字节/请求，永不释放、永不饱和。** 三场连续 60s c128 风暴后 backend worker RSS 线性爬升 3.91 → 7.48 → 10.97 GB。正式对比表的"全栈最重 5,350 MiB"本质是**这个泄漏在 S1 风暴后的直接读数**（会话脚本 S1 先于 S2，S2 观测窗读到的是泄漏后的 backend）—— **修复此泄漏 = 全栈 RSS 回到 ~1.1 GB 级（低于 Keycloak 1.75GB）**，"最重栈"警示可摘。

## 2. 证据链

| 步骤 | 观测 | 推论 |
|---|---|---|
| 基线（空闲，setup 后） | worker（fork 子进程，26 线程）VmRSS **17.5 MB** | 干净起点 |
| S1 风暴中（c128×60s，~85k QPS） | t+15s: 1.48GB → t+35s: 2.71GB → t+65s: 3.93GB | ~65 MB/s 恒定增长 |
| 风暴后 30s / 90s | RSS **纹丝不动**（3,933,848 kB 整） | 不是延迟释放，是永久留存 |
| 量化 | (3.93GB−17MB) ÷ 5.39M 请求 ≈ **730 B/请求** | 每请求常数留存 |
| `/proc/8/smaps` 分解 | 3,806 MiB 匿名私有脏页；直方图 **70 个 ≈64MB 段（合计 3.39GB）** + 10 个 32-63MB | glibc 每线程 arena 的典型停放布局（26 线程 → glibc 上限 8×8 核+main ≈ 70 arena），是**停放位置**不是根因 |
| 诊断臂 `MALLOC_ARENA_MAX=2` | RSS 减半至 1.83GB，**但吞吐也减半（85k→40k QPS，arena 锁竞争）**；1.83GB÷2.49M 请求 ≈ 同样 730 B/请求 | ① capping 非修复方案；② 留存主体随**请求数**而非 arena 数缩放 → 真实累积，非碎片 |
| 三连风暴（同栈不重置） | 3.91 → 7.48 → 10.97 GB 线性 | **无界**——不是有界缓存，是泄漏；长跑必 OOM |

## 3. 影响面判定

- **S1 专属**：先跑 S2 的会话里 backend 只有 ~270-300MB（本次三臂 A/B 的 TSV 实测）——泄漏在 discovery 请求路径（或其独有中间件），不在 token/introspect/userinfo 路径。
- **生产风险**：discovery 是高频无状态端点，~730B/请求意味着每天亿次级 discovery 请求的服务会以 GB/小时 级泄漏 —— **这是个必须修的生产 bug**，不止是基准数字问题。
- **对比表口径**：D7 数字被泄漏污染（backend 读数=泄漏量）—— 修复后需重刷正式表（或至少 RSS 节）。

## 4. 候选泄漏点（未验证，供修复立项）

discovery 路径每请求的分配链：HttpRequest/HttpResponse、**DiscoveryController 每请求重建 ~40 字段 JSON 文档**（`DiscoveryController.cc:120-188`）、Json::Value 树、字符串拷贝。~730B/请求的量级与"一个小 JSON 对象树或一个 shared_ptr 控制块 + 字符串"相符。定位手段（按性价比）：
1. **Valgrind leak-check mini-run**（valgrind 构建 + c8×10s 小风暴 + `--leak-check=full`）—— 直接给出分配栈，一步定谳；
2. 代码审查 discovery 链上的 static/全局累积（metrics label map、request-id、任何 per-request 插入却无淘汰的容器）；
3. ASan 构建（leak sanitizer）跑同样 mini 风暴。

## 5. 附：调查过程踩的坑（已入记忆）

- **gdb `call malloc_stats()` 打死了多线程活进程**（26 线程 ptrace 停停态下碰 malloc 锁）—— 监督进程自动重生了 worker（进程弹性顺带得到验证）；此后改用零侵入的 /proc/smaps 解析。
- `docker ps --filter ancestor=<image>` 在共享镜像时会把 bench backend 一并圈进来（误停一次，`docker start` 恢复）。
- busybox awk 无 `strtonum` —— smaps 解析拉到宿主机用 python。

## 6. 状态

| 项 | 状态 |
|---|---|
| 复现 + 量化（730B/req、无界、零衰减） | ✅ 本报告 |
| 机制层（arena 停放 vs 真累积） | ✅ 诊断为真累积（capping 实验排除碎片主导） |
| 泄漏点定位（文件:行） | ⬜ 待修复立项（§4 手段） |
| 修复 + 重刷对比表 RSS 节 | ⬜ 依赖定位 |
