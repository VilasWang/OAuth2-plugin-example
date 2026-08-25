# Backend 内存留存调查报告（终局：drogon session 留存，非泄漏）

> 日期：2026-08-22（终局改写 v2，修正 ASan 污染数据）· 分支 `feat/competitor-benchmark` · **结论性质：机制定谳 + 缓解交付 + 根修路径已录**
> 起因：正式对比表（2026-08-21）中 Fulla 全栈 RSS 5,350 MiB 四家最重，破案发现 backend 容器均值 4,711 MiB 才是大头。
> ⚠️ **本报告早前版本称"无界泄漏"——终局证据推翻该定性：留存完全由 session TTL 有界，淘汰机制正常。** 早前定性错误的原因：3600s TTL 远大于所有测试时程（最长 3 分钟），TTL 内零衰减与"永不衰减"在观察窗内不可区分。
> ⚠️ **v2 修正（ASan 镜像污染事故）**：调查中期三次 ASan 诊断构建覆写了 bench 管线的 `latest` 镜像标签，导致后续 TTL 验证 / A/B 对照全部跑在 ASan 构建上。ASan 吞吐 ~1/3（86k→18k）、分配块膨胀 ~48%（750B→1.1KB）。本报告已将留存公式修正为生产构建实测值 **750 B/req**；-24% session 吞吐税的方向可靠（同臂相对对照），生产构建绝对值待重测。

## 1. 结论（TL;DR）

**根因**：drogon 上游设计行为（[drogon#278](https://github.com/an-tao/drogon/issues/278)）——`enable_session: true` 时每个**不带会话 cookie** 的请求都会创建一个 Session 并在 SessionManager 的 CacheMap 中持有到 `session_timeout` 到期。OAuth2 服务器的机器流量（token/introspect/userinfo/discovery）全都不带 cookie，因此按请求付费。

**实测代价**（生产 LTO 构建，2026-08-22）：
- 留存 = **~750 B/请求**（三场风暴 744/755/759 B），稳态常驻 = `API_QPS × session_timeout × 750 B`
- discovery 吞吐税 = **~-54%**（生产 LTO 构建同窗口 6 轮交错 OFF/ON：164.6k → 76.3k QPS；此前 ASan 构建测得 -24% 系 ASan 压缩了相对占比）
- **真框架天花板 = ~165k QPS**（首次无 session 测量；历史 87-104k 系 session 限制值）
- 对比表的 5,350 MiB = S1 风暴（540 万请求）后读到的 session 留存（backend 4.7 GB ≈ 5.4M × 750B + 基线 ✓）

**已交付缓解**：bench 档 `session_timeout: 30`（e13041f）—— 留存封顶 `QPS×30×750B`（85k QPS 下 ~1.9 GB），交互流语义无损（S4 全阶梯验证）。**生产指引**（带验证公式的尺寸速查表）见 `docs/ops/deployment.md` §性能调优-3。**根修路径**（上游惰性/按路径建 session）录于 `upstream-drogon-session-issue.md`（待 gh 认证恢复后发 issue）。

## 2. 证据链（终局版）

| 步骤 | 观测 | 推论 |
|---|---|---|
| S1 风暴（c128×60s，**生产 LTO**） | worker 17.5 MB → 3.93 GB；风暴后 90s 零衰减 | 每请求留存 ≈**750B**（生产构建），观察窗内不释放 |
| 三连风暴（**生产 LTO**） | 3.91 → 7.48 → 10.97 GB 线性 | 窗口内无界（但 < TTL 时程，见终局修正） |
| `/proc/smaps` | 3.8 GB 全匿名脏页，70×64MB glibc arena 段 | arena 停放布局（非根因） |
| `MALLOC_ARENA_MAX=2` 诊断 | RSS 减半、吞吐也减半、每请求留存率不变 | 真累积非碎片；capping 非修复 |
| 404 路径风暴 | 同样按请求留存 | 框架级机制，非控制器代码 |
| **LSan（USR1 钩子，ASan 构建）** | 41.5 万请求后**仅 48 字节不可达泄漏** | 留存全部"可达" = 活容器持有 |
| **ASan 活堆剖面** | `SessionManager::getSession` 655,245 次分配 = **每请求恰好 1 个 Session**（128B/对象） | 定位到 drogon session 机制 |
| 配置核查 | 全配置模板 `enable_session: true, session_timeout: 3600` | TTL 远大于测试时程 → 观察窗内像无界 |
| **TTL=120 + 130s idle 二次剖面** | 活堆 **106 MB → 14.9 MB 塌缩** | **淘汰正常工作，留存完全 TTL 有界 —— "泄漏"定性推翻** |
| ⚠️ **ASan 镜像污染** | 三次 ASan 诊断构建覆写 `latest`，后续所有风暴跑在 ASan 上（86k→18k、1.1KB/req） | **绝对数据无效**；相对结论（TTL 等价、session 税方向）因同臂对照仍成立 |
| ASan 构建 OFF/ON/OFF 同窗口三连 | 30.6k / 23.2k / 30.2k QPS | session 机制吞吐税 -24%（方向可靠，生产绝对值待重测） |
| **生产 LTO 重建验证** | S1 c128×30s → **86.7k QPS**，RSS 1.9GB ≈ 2.6M×750B ✓ | 生产构建恢复确认 |
| **生产 LTO 6 轮交错 OFF/ON** | OFF 165k / ON 76k QPS（各 3 轮均值） | **session 吞吐税 -54%**（ASan 的 -24% 系基线压缩导致低估）；真框架天花板 ~165k |

## 3. 处置清单（全部落地）

| 项 | 状态 |
|---|---|
| 机制定谳 + 量化（**750B/req、公式、-54% 税（生产 LTO 6 轮交错）、真天花板 165k**） | ✅ 本报告 v3 |
| bench 档 TTL=30（净两行，e13041f） | ✅ |
| S4 语义验证 | ✅ |
| 部署文档调优指引（**750B 公式** + 尺寸速查表） | ✅ deployment.md §性能调优-3 |
| 根修路径记录（上游 issue 正文备妥） | ✅ `upstream-drogon-session-issue.md` |
| **生产构建 session 吞吐税重测**（替代 ASan 的 -24%） | ⬜ 待生产构建 A/B |
| 四产品对比表重刷（RSS 节将大幅回落） | ⬜ 待良好机器窗口（TTL=30 档 + 生产镜像） |
| research.md "全栈最重"警示更新 | ⬜ 随对比表重刷一并 |

## 4. 附：调查过程踩的坑（全部已入记忆）

- **⚠️ ASan 镜像污染（最大教训）**：三次 ASan 诊断构建经 `docker compose build` 无声覆写 `latest` 标签 → 后续所有风暴/TTL 验证跑在 ASan 构建上（吞吐 86k→18k、分配 750B→1.1KB）。**预防**：诊断构建必须用独立 `docker build -t <专用标签>` 而非 compose 管道，或每次诊断后立即重建生产镜像。
- gdb `call malloc_stats()` 打死过多线程活进程（监督进程自动重生验证了进程韧性）；
- **LSan 拒绝在 ptrace 下运行且为致命错误**（会 abort 被测进程）—— 改用 `#ifdef FULLA_LEAK_DIAG` 的 SIGUSR1 钩子（`main.cc`，随诊断设施入库）；
- worker 的 SIGTERM/SIGINT 优雅退出在 ASan 下 SEGV/system_error（trantor 关闭期竞态，又一独立待查项）；空 `redis_clients` 时 HealthController 空指针 SEGV（待查项）；
- Windows Docker 把不存在的挂载源自动建为**目录**；
- 实验方法论：跨臂对照必须同窗口背靠背 + 负控（本轮 OFF/ON/OFF 三连即此范式）；观察窗必须覆盖 TTL 边界再下"永不衰减"结论；**绝对数据必须声明构建类型**（ASan vs 生产）。

## 5. 状态

| 项 | 状态 |
|---|---|
| 根因（机制级 + 配置级） | ✅ 定谳 |
| 缓解（bench 档 + 生产指引） | ✅ 交付 |
| 根修（上游惰性 session） | 📋 已录，blocked on upstream |
| 对比表重刷 + 警示更新 | ⬜ 待机器窗口 |
