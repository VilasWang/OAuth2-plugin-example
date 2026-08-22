# AuthForge 竞品性能基准对比设计

> **版本**: v1.1（2026-08-15 评审修订：新增 M0 前置参数化、S5 池规模修正、GC 抖动场景钉死、AuthForge 同 session 重跑、Zitadel JWT-profile 认证说明、warmup 口径对齐入仓数据）
> **日期**: 2026-08-15
> **文档性质**: 技术设计（Phase 0.5 落地蓝图，**非代码**——实施见 §七 milestone）
> **上游规划**: [演进方案 §三 Phase 0](../productization-evolution-plan.md) P0「自托管竞品对比基准」
> **前置依赖**: [基准设施设计](benchmark-facility-design.md) M1–M4 已交付（S1–S6 自测数据已入仓 `benchmarks/results/`）
> **验证对象**: [调研报告 §3.1](../productization-research.md) 竞品列（Keycloak/Ory）的量级参考数字

---

## 零、TL;DR

- **做什么**：在**同一台机器、同一套 wrk 阶梯、同一个 PostgreSQL 后端**下压 Keycloak / Ory Hydra / Zitadel，与 AuthForge 已入仓的自测数据（`benchmarks/results/SUMMARY.md`）产出同口径对比表。
- **比什么**：S1 discovery / S2 client_credentials / S3 introspect / S5 refresh_token / S6 userinfo 五个单步场景 + 冷启动 + 稳态 RSS + **GC 抖动长跑**（5 分钟 P99 时间序列——AuthForge 无 GC 的核心差异化证据）。
- **不比什么**：S4 auth_code（各产品登录/consent 流程不可 wrk 统一驱动）；Auth0（SaaS，无法自托管）；竞品的极限调优配置（一律用官方推荐生产配置）。
- **核心原则**：公平性优先于数字好看。竞品社区会质疑，方法论必须无懈可击——同硬件、同并发阶梯、同后端、各产品官方推荐配置、脚本全部入仓可复现。
- **验收**：四家同口径对比表（QPS / P99 / 稳态 RSS / 冷启动 / GC 抖动）落盘 `benchmarks/competitors/results/COMPARISON.md`；第三方按 README 一键复现。

---

## 一、目标与非目标

### 1.1 目标

| # | 目标 | 衡量 |
|---|------|------|
| G1 | **同环境竞品对比数据**：把调研报告 §3.1 的竞品列（"来自各产品社区公开基准，非同环境对比"）替换为同环境实测 | §六验收 ✅ COMPARISON.md |
| G2 | **验证差异化叙事**：C++ 无 GC / 低内存 / 低尾延迟的卖点是否有同环境数据支撑 | GC 抖动长跑 + 稳态 RSS 对比 |
| G3 | **可复现**：第三方按 `benchmarks/competitors/README.md` 在同规格机器跑出误差 <15% 的数据 | 复现门槛（沿用自测设施 AC1） |
| G4 | **诚实修订**：若某维度 AuthForge 不领先，据实修订调研报告 §3.1/§3.2 的卖点排序 | 报告更新 |

### 1.2 非目标

| # | 非目标 | 为什么 / 归属 |
|---|--------|--------------|
| N1 | Auth0 / Okta 等托管竞品 | SaaS 无法自托管、无法控制环境；其公开数字不可复现。仅保留在调研报告的量级参考里 |
| N2 | S4 auth_code 场景对比 | 各产品的登录页/重定向/consent 交互流完全不同，wrk 无法统一驱动（详见 §四 D4） |
| N3 | 竞品极限调优 | 一律官方推荐生产配置。极限调优对比是军备竞赛，无公信力；官方文档链接随结果附上 |
| N4 | 功能/协议覆盖度对比 | 属产品能力审计（iam-architecture-audit.md），与性能无关 |
| N5 | AuthForge 阶梯数据重跑 | 同 session 仍会重跑（见 §5.1 v1.1 修正：gcjitter/RSS/冷启动必采 + R7 消漂移）；"复用"仅指沿用同一 runner/口径，2026-08-12 入仓数据降级为历史基线 |

---

## 二、背景：为什么现在做

### 2.1 Phase 0 已完成自测，对比是最后缺口

Phase 0（benchmark M1–M4）已于 2026-08-12 交付：S1–S6 六场景、40 个 JSON、承重验证报告（`benchmarks/results/SUMMARY.md`）。AuthForge 自身数字已可信。

调研报告 §3.1 的竞品列（Keycloak ~10–20k QPS、Ory ~30–50k QPS）标注为"社区公开基准，非同环境对比，仅作量级参考"。**没有同环境对比数据，"比 Keycloak 快 N 倍"就不能出现在任何对外物里。**

### 2.2 AuthForge 自测基线（对比基准，2026-08-12 实测）

| 场景 | 稳态 QPS | 稳态 P99 | 错误率 |
|------|---------|---------|--------|
| S1 discovery | 86,332 | —（低并发 <1ms） | 0.006% |
| S2 client_credentials | 8,915 | 8ms | 0.000% |
| S3 introspect | 17,132 | 12ms | 0.000% |
| S5 refresh_token | 1,982 | 26ms | 0.000% |
| S6 userinfo | 16,674 | 12ms | 0.000% |

环境：WSL2 8 vCPU / 16GB / PG 连接池 25 / Redis 20 / wrk 4.1.0 / 阶梯 2→128。
详见 `benchmarks/results/SUMMARY.md`。

### 2.3 现有可复用资产

| 资产 | 复用方式 |
|------|---------|
| `run-scenario.sh` 阶梯 runner（warmup→measure→JSON） | 竞品场景直接传不同的 `.lua`；runner 加**可选**参数（`READY_PATH`/`RESULTS_DIR`/`WRK_LIB_DIR`/`--reissue` 钩子，见 M0.1），单一实现不复制第二份 |
| `parse-wrk.py`（wrk 文本→schema v1 JSON） | 场景无关，仅加 `--product/--product-version` 透传参数（M0.2） |
| `observe/docker-stats.sh` + `scrape-metrics.sh` | docker-stats 加 `CONTAINER_GLOB` 参数（M0.3）采竞品容器 RSS/CPU；scrape-metrics 仅 AuthForge 可用（竞品无 `/metrics` 端点，观察项拆分） |
| `measure-cold-start.sh` 模式 | 每家竞品一个等价的冷启动计时脚本 |
| `lib/gen-tokens.py` 思路 | 竞品 token 池生成（各家 introspect 的 token 须由其自身签发，见 D5） |
| docker-compose 底座（PG15） | 竞品 compose 复用同一 PG 版本与连接池配置 |

---

## 三、关键约束与设计决策

### D1 — 公平性三同原则（同硬件 / 同工具 / 同后端）

**这是整个方案的可信度基础。**

| 维度 | 统一值 | 说明 |
|------|--------|------|
| 硬件 | 同一台机器（Phase 0 用的 WSL2 8 vCPU/16GB，或后续专用裸机） | 四家依次跑，中间 `docker compose down -v` 清场 |
| OS/内核 | 同一 WSL2 Ubuntu | — |
| 压测工具 | wrk 4.1.0，同一份阶梯参数（2→4→8→16→32→64→128）、warmup 5s / measure 10s（与入仓自测数据口径一致；Keycloak warmup 60s，见 D2 豁免） | 复用 `run-scenario.sh`，不写第二套 runner |
| 后端存储 | PostgreSQL 15（同一 image tag），连接池对齐 25 | 竞品各自支持的连接池上限可能 <25，取 `min(25, 官方上限)` 并在结果中标注 |
| 网络拓扑 | localhost cross-container（wrk 在宿主机） | 与自测一致 |
| 结果格式 | schema v1 JSON（`parse-wrk.py`） | 四家数据同构，`run-comparison.sh` 才能聚合 |
| 端口 | 各家栈固定端口（authforge 5555 / Keycloak 8080 / Hydra 4444+4445 / Zitadel 8080） | 串行执行互不冲突；每家 setup 前置断言端口空闲 |

### D2 — 竞品配置 = 官方推荐生产配置（不调优、不调差）

**依据**：极限调优对比无公信力；故意调差是学术造假。

| 竞品 | 配置基线 | 官方出处 |
|------|---------|---------|
| Keycloak | `start --optimized` + PostgreSQL，内存按官方建议 2GB 限额 | [Running Keycloak in a container](https://www.keycloak.org/server/containers) |
| Ory Hydra | 官方 docker-compose + PostgreSQL DSN | [Ory Hydra docs](https://www.ory.sh/docs/hydra/self-hosted/deploy-hydra) |
| Zitadel | 官方 compose（`setup mode` 初始化 + PostgreSQL） | [Set up ZITADEL with Docker Compose](https://zitadel.com/docs/self-hosting/deploy/compose) |

每家竞品的 `setup.sh` 头部注释必须附官方文档链接；偏离官方默认的每一项（如连接池对齐）单独注释理由。

**JVM 预热豁免**：Keycloak 的 JIT/GC 需要更长预热。warmup 对 Keycloak 延长到 60s（其余三家与 AuthForge 自测口径一致取 5s，测量时长统一 10s），在结果中标注——这不是偏袒，是给 JIT 编译时间，否则测的是"未编译的解释执行"。

### D3 — 场景映射：功能等价，不是路径等价

各产品端点路径不同，映射到**同一功能**的端点（§四矩阵）。关键约束：

- **S3 introspect 的 Ory 特例**：Hydra 的 introspect 在 admin 端口（4445）而非 public 端口，且生产部署中 admin 端口通常不对外。为公平，四家都测 introspect 但**在 COMPARISON.md 中标注 Ory 的 admin-port 语义差异**。
- **认证方式**：client_credentials 用各产品的标准 client 认证（Basic 或 post，按其声明）。
- **Zitadel S2 特例（JWT profile）**：Zitadel 的官方 M2M 路径是 Service User + private_key_jwt（token 端点**不支持** Basic 认证的 client_credentials）。wrk Lua 无法签名 JWT，故 setup 阶段用 python（pyjwt + cryptography，WSL 已具备）预签 client_assertion 池，exp 覆盖整个跑数窗口；若 Zitadel 强制 jti 单用则每档重签（等价 S5 的 --reissue 机制）。COMPARISON.md 附录注明"JWT profile 是 Zitadel 官方推荐的机器认证，功能等价"。

### D4 — 排除 S4 auth_code：wrk 不可驱动

AuthForge 的 S4 是 `login → token` 两步 form POST（headless 可驱动）。但：
- **Keycloak** auth_code 需要走其登录页 HTML 表单 + JS
- **Ory Hydra** 需要外部 login/consent app 配合（Hydra 本身无登录 UI）
- **Zitadel** 有自己的登录会话流

统一驱动需要真实浏览器（Playwright），测的是"登录页渲染"而非"token 签发"。**首期排除 S4，COMPARISON.md 注明限制**；若后续需要，用"预签发 code + 单步 token 交换"近似（各产品预签发方式不同，复杂度高，Phase 0.6 再议）。

### D5 — 竞品 token 池必须由其自身签发（不能 SQL 直插）

AuthForge 自测的 S3/S6 用 SQL 预种 token（`gen-tokens.py`）。**竞品不行**：
- Keycloak 的 access token 是签名的 JWT，哈希/密钥格式私有
- Hydra/Zitadel 同理

**方案**：每家竞品的 `setup.sh` 通过其**自身的 token 端点**批量签发 token，把活跃 token 写入 token 池文件，Lua 场景脚本复用同一套 token-pool 逻辑（线程切片，复用 `s3-introspect.lua` 模式）。

**池规模（v1.1 修正——两类池口径不同）**：
- **S3/S6 池（可复用）**：token 只读验证不消耗，N=2000 足够；批量签发本身约 1–2 分钟（并行 xargs -P8 更快）。
- **S5 池（单发单耗）**：每个 refresh token 用一次即失效，池必须 ≥ 该档 QPS × 测量时长 × 1.3 余量。AuthForge 自测的实测口径：20,000 池 ÷ 10s ≈ 1,982 QPS（池刚好覆盖测量窗口）。竞品每档前须**重发池**（`run-scenario.sh` 新增 `--reissue "<cmd>"` 钩子，等价自测的 SQL `--reseed`，但走各家 API）。
- **RT 不能来自 client_credentials**：RFC 6749 §4.4.3 规定该 grant 不得签发 refresh token。竞品 RT 池须经用户上下文流程获取——Keycloak 用 ROPC（direct access grants）；Hydra 用 accept 流（见 M2）；Zitadel 用 Session API/auth_code（见 M2）。

### D6 — GC 抖动 = 长跑 P99 时间序列（AuthForge 核心差异化证据）

单次 30s 跑看不出 GC 周期。设计专门的**长跑测试**：

```
每家：c=32 固定，持续 5 分钟，场景钉 S6 userinfo
（v1.1 修正：S5 不可用——池会耗尽；S2 有写放大——AuthForge 每请求落库一条 token，
5 分钟 ~8k QPS ≈ 240 万行，对四家工作负载构成不对称；S6 是读路径、token 池可复用、
四家功能等价，是最公平的载波。Hydra 若 S6 标 N/A 则降级 S2 并在结果中标注。）
采集：每 10s 窗口记录一次该窗口的 P99（wrk 不支持原生分段 → 30 个串行 10s 段近似）
输出：P99 随时间的曲线（JSON 数组）
预期：Keycloak 出现周期性 P99 尖峰（GC STW）；Ory 出现 Go GC 小尖峰；AuthForge 平线
```

实现：`benchmarks/competitors/run-gc-jitter.sh`——循环调用 wrk `-d10s` 30 次串联，每段 parse 出 P99 存数组。**这是对外叙事最有力的一张图**（"零 GC 抖动"的可视化证据）。

### D7 — 内存口径统一：容器全栈 RSS + 标注逻辑层

Phase 0 承重验证发现 AuthForge 容器 RSS ~2.4GB（含 Drogon 连接池/共享库 COW）与"50–120MB"声称口径不匹配。对比方案统一口径：

| 口径 | 采集 | 用途 |
|------|------|------|
| 容器全栈 RSS | `docker stats` 稳态采样（复用 observe 脚本） | **对比表主口径**——四家同口径可比 |
| 进程 PSS（可选） | `smem` / `/proc/*/smaps_rollup` | 消除 COW 共享页重复计数的补充口径 |

在 COMPARISON.md 明确写"全栈容器 RSS，含各自运行时+连接池"，避免口径争议。

---

## 四、对比场景矩阵

### 4.1 场景 × 竞品端点映射（已按官方文档核实）

| 场景 | 功能 | AuthForge | Keycloak | Ory Hydra | Zitadel |
|------|------|-----------|----------|-----------|---------|
| **S1** discovery | OIDC 发现文档 | `GET /.well-known/openid-configuration` | `GET /realms/{r}/.well-known/openid-configuration` | `GET /.well-known/openid-configuration` (public :4444) | `GET /.well-known/openid-configuration` |
| **S2** client_credentials | 机器间 token | `POST /oauth2/token` | `POST /realms/{r}/protocol/openid-connect/token` | `POST /oauth2/token` (public :4444) | `POST /oauth/v2/token` |
| **S3** introspect | token 内省 | `POST /oauth2/introspect` | `POST /realms/{r}/protocol/openid-connect/token/introspect` | `POST /admin/oauth2/introspect` (admin :4445) ⚠️ | `POST /oauth/v2/introspect` |
| **S5** refresh_token | token 刷新 | `POST /oauth2/token` (refresh) | `POST /realms/{r}/protocol/openid-connect/token` (refresh) | `POST /oauth2/token` (refresh) | `POST /oauth/v2/token` (refresh) |
| **S6** userinfo | 用户信息 | `GET /oauth2/userinfo` | `GET /realms/{r}/protocol/openid-connect/userinfo` | `GET /userinfo` (public :4444) | `GET /oidc/v1/userinfo` |
| ~~S4~~ auth_code | 用户登录 | ~~`login→token`~~ | ~~登录页不可 headless 驱动~~ | ~~需外部 consent app~~ | ~~登录会话流~~ |

> ⚠️ Ory introspect 的 admin-port 语义差异在结果中标注。Keycloak realm 名、Hydra public/admin 双端口、Zitadel 的 instance domain 都在各自 setup 脚本中固定为常量。

端点出处：
- Keycloak: [OpenID Connect endpoints](https://www.keycloak.org/docs/latest/securing_apps/)（`/realms/{realm}/protocol/openid-connect/*`）
- Ory Hydra: [API docs](https://www.ory.sh/docs/hydra/reference/api)（public :4444 / admin :4445）
- Zitadel: [OpenID Connect Endpoints](https://zitadel.com/docs/apis/openidoauth/endpoints)

### 4.2 指标矩阵

| 指标 | 采集方式 | 对比表列 |
|------|---------|---------|
| 稳态 QPS（err<0.01% 最高档） | wrk 阶梯 + parse-wrk.py | ✅ |
| P50 / P95 / P99（稳态档） | wrk `--latency` | ✅ |
| 错误率 | wrk non-2xx | ✅（门槛列） |
| 冷启动（→health 200） | 各家等价计时脚本 | ✅ |
| 稳态容器 RSS | docker stats 采样 | ✅ |
| GC 抖动（5min P99 曲线） | run-gc-jitter.sh | ✅（专项小节） |
| driver CPU | run-scenario.sh 现有采样 | ✅（可信度标注） |

---

## 五、测试策略

### 5.1 执行顺序（单机串行，防互相干扰）

```
1. AuthForge    — 同 session 重跑（v1.1 修正：gcjitter/RSS/冷启动是 AC1/AC2 的必采项，
                  Phase 0 未采过 gcjitter；且 R7 要求四家同一 session 连续跑以消除跨日
                  环境漂移。2026-08-12 入仓数据保留为历史基线，同 session 新数据用于对比）
2. Keycloak     — setup → 阶梯 → 长跑 → 冷启动 → teardown
3. Ory Hydra    — 同上
4. Zitadel      — 同上
每家之间 docker compose down -v + 确认无残留容器/卷/网络
```

`run-comparison.sh --fresh` 全串行执行；`--only keycloak` 支持单家补跑。

### 5.2 竞品 setup 约定（每家一个 setup.sh）

每个 `setup.sh` 必须：
1. 启动该竞品 + PostgreSQL（对齐连接池，见 D1）
2. 等待健康（各家的 ready 探针：Keycloak `/realms/master`、Hydra `/health/ready`、Zitadel `/debug/healthz`）
3. 初始化配置（realm/client/用户——用各家 CLI 或 admin API）
4. **批量签发 token 池**（D5：N 次 client_credentials 请求 → `access_tokens.txt`）
5. warmup 校验（一个 token 请求确认管线通）

### 5.3 长跑 GC 抖动（D6）

参数：`c = 各家稳态拐点档`，30 × 10s 段（共 5 分钟），段间无间隔。
输出：`results/<date>-<sha>-<product>-gcjitter.json`（P99 数组 + 段时间戳）。

### 5.4 冷启动

各家独立计时：`docker compose up -d <idp>` → 轮询 ready 探针 200。
记录两模式（含/不含 DB 预热）与自测 `measure-cold-start.sh` 对齐。

### 5.5 环境元数据

结果 JSON 的 `env` 块沿用 schema v1，新增 `product` / `product_version` 字段（parse-wrk.py 加一个透传参数），COMPARISON.md 表头列出版本。

---

## 六、验收标准（可勾选）

> ✅ 2026-08-17 全部通过（M0–M3 交付，四产品同 session 实测 2026-08-17，COMPARISON.md 由 gen-comparison.py 生成）。
> 🔄 2026-08-21 全量重跑刷新（同一设施，AuthForge 换性能优化后基准档——wave-1/2 + LTO，见 G1 交付注）：**五场景全部领先**（此前 S5/S6 落后——S5 系测量预算伪影已修口径、S6 经 wave-2 用户/角色缓存反超）；GC 节新增跨产品环境噪声互证（四家同款尖峰）。调研报告 §3.1 已同步引用新表。

| # | 验收项 | 衡量 | 状态 |
|---|--------|------|------|
| AC1 | **四家同口径对比表**：S1/S2/S3/S5/S6 × {QPS, P99, RSS, 冷启动} 入仓 `benchmarks/competitors/results/COMPARISON.md`，每行带产品版本号 | COMPARISON.md | ✅ |
| AC2 | **GC 抖动曲线**：四家 5 分钟 P99 时间序列 JSON + 对比小节（AuthForge 是否平线、Keycloak 是否有周期尖峰） | gcjitter JSON + 小节 | ✅（结论与预期相反，见 G4 注记：GC 语言全平线、AuthForge 有环境层秒级尖峰——已在报告与研究报告中诚实修订） |
| AC3 | **可复现**：`run-comparison.sh` 一键串行跑四家；README 含环境要求与复现步骤 | 复现指引 | ✅（`benchmarks/competitors/README.md`） |
| AC4 | **公平性声明**：每家竞品的配置来源（官方文档链接）、偏离默认的每一项、warmup 差异（Keycloak 60s）均显式标注 | COMPARISON.md 附录 + setup.sh 注释 | ✅ |
| AC5 | **诚实修订**：调研报告 §3.1 竞品列更新为"同环境实测"，§3.2 卖点若被证伪则收敛 | research.md 更新 | ✅（S5/S6 与 GC 抖动主张按实测收敛，见 §3.1/3.2 修订） |
| AC6 | **S4 排除声明**：COMPARISON.md 注明 auth_code 场景的方法限制 | 限制小节 | ✅（附录 B.1） |

### 实施勘误记录（v1.2，2026-08-17 落地时修订）

实施过程中偏离本设计 v1.1 的决策，全部为公平性/可行性修正：

1. **Zitadel 版本 v2.71.19 → v4.17.1**：v2.71 已落后两个大版本（当前稳定线 v4，与 Keycloak 26 / Hydra 26 同代），且 v4 为 eventstore/投影性能重写。测旧版会失真贬低 Zitadel，不可辩护。首次尝试的 `v1.80.0-v2.9-amd64` tag 实为 v1 时代 CockroachDB-only 镜像，废弃。
2. **Zitadel S2 认证路径修正**：设计 D3 原表述"client_assertion（JWT profile）"在 v2.71/v4 实测均不可用——Zitadel token 端点对机器用户的 client_credentials 只走 Basic-secret（每请求哈希），官方 M2M 路径是 **RFC 7523 jwt-bearer 授权**（`grant_type=urn:ietf:params:oauth:grant-type:jwt-bearer&assertion=…`）。S2/S3 的等价性标注已按此更新。
3. **Zitadel S3 认证换私钥**：#6220 官方建议——secret 认证每请求做密码哈希（CPU 瓶颈）；改用 OIDC app + private_key_jwt。实测对比：Basic 路径 S3 无法过错误门，私钥路径 2.9k QPS 零错误。
4. **投影平复门**：mint ~2000 token 后立即开压，Zitadel CQRS 投影追赶会使 S1 出现 500 风暴（c=16 达 99.99% 错误）。run-all 前置 discovery 冒烟门（clean 才开跑），消除该伪影。
5. **S5 Zitadel = N/A**（DG-2 提前裁决）：机器用户无 refresh token（RFC 6749 §4.4.3）、password grant 已移除、Session API→auth_code 属用户交互流（D4 同类排除理由）。限制小节注明。
6. **docker-stats.sh 竖线 glob 回归修复**：bash 5.2 下 `case $x in $GLOB)`（GLOB 含 `|`）不再展开为多分支——AuthForge 侧 RSS 采样自 M0 参数化起一直空采；改为拆分逐个匹配。该回归同时解释了 v1.1 后 AuthForge RSS 数据缺失。
7. **AuthForge S2 scope 跟随 #43**：seed 摒弃 legacy `read/write`，bench 校验与 s2 lua 改用 `tokens:read`（同码同测，非口径变化）。

---

## 七、实施计划（4 个 milestone，每步带验收标准）

> v1.1：M0 为评审新增前置——共享设施的参数化改造（向后兼容，不带新 env 时行为不变）。

### M0 — 共享设施参数化（前置，~0.5 天）

| # | 步骤 | 内容 | 验收标准 |
|---|------|------|---------|
| M0.1 | `run-scenario.sh` 参数化 | (a) `READY_PATH` env（默认 `/health/ready`）——健康门探针可换竞品探针；(b) `RESULTS_DIR` env（默认 `benchmarks/results`）；(c) `WRK_LIB_DIR` 导出改为 `${WRK_LIB_DIR:-$BENCH_DIR/lib}`（竞品 lua 指向自家 lib）；(d) `BENCH_PRODUCT`/`BENCH_PRODUCT_VERSION` env 透传给 parse-wrk.py；(e) 通用 `--reissue "<cmd>"` 钩子：每档 warmup 前与 measured 前各执行一次（竞品 S5 用 API 重发 token 池，替代 AuthForge 的 SQL `--reseed`）；(f) `--observe` 拆为 `--observe-stats`（仅 docker-stats）与 `--observe-metrics`（仅 scrape-metrics；竞品无 `/metrics` 不用） | **AC-M0.1a** 不带任何新 env 跑 AuthForge S1 单档（c=2, -d10s）：行为与改造前一致（JSON schema、默认值、输出路径全不变）；**AC-M0.1b** `READY_PATH=<任意 200 路径> RESULTS_DIR=<tmp> BENCH_PRODUCT=x` 时健康门走新路径、JSON 落新目录且 env.product=x；**AC-M0.1c** `--reissue "touch $TMP/marker"` 冒烟：每档产生两个 marker（warmup 前+measured 前） |
| M0.2 | `parse-wrk.py` 透传 | `--product`/`--product-version` CLI 参数 → env 块新增 `product`/`product_version` 字段（缺省 `authforge`/空） | **AC-M0.2** wrk 样例文本经管道解析，JSON 含两字段且值正确；不传参时缺省值不破坏现有聚合 |
| M0.3 | `docker-stats.sh` 容器过滤参数化 | `CONTAINER_GLOB` env（默认保持 `*oauth2-backend*|*oauth2-postgres*|*oauth2-redis*` 语义） | **AC-M0.3** `CONTAINER_GLOB='*keycloak*'` 采样时输出含 keycloak 容器行、不含 authforge 行 |
| M0.4 | schema 文档同步 | `result-schema.md` env 块补 `product`/`product_version` 字段说明 | 文档字段表与实现一致（对照检查） |

### M1 — Keycloak 对比（最重，先啃硬骨头，~2 天）

配置基线（D2）：`quay.io/keycloak/keycloak:<pin 版本>` + `start --optimized` + `--memory 2G`（官方容器建议），PostgreSQL 用与自测**相同 image tag** 的 `postgres:15-alpine`，端口 8080。

| # | 步骤 | 内容 | 验收标准 |
|---|------|------|---------|
| M1.1 | `docker-compose.yml` | keycloak + postgres；healthcheck 打 `/realms/master`；卷/网络显式前缀 `kc-bench-` | **AC-M1.1a** `up -d` 后探针 200；**AC-M1.1b** `down -v` 后 `docker ps -a`/`docker volume ls`/`docker network ls` 无 kc-bench 残留 |
| M1.2 | `setup.sh` | 等健康 → `kcadm.sh` 建 realm `bench`、client `bench-svc`（confidential + service account + introspection 权限）、user `bench-user`（direct grant）→ **校准跑**（c=8 单档 S2/S5/S6 估 QPS）→ 按 `池 = QPS×10s×1.3` 生成 RT 池（ROPC 批量签发，xargs -P8 并行）+ AT 池 ≥2000（S3/S6 复用）→ warmup 验证一发 token 请求 | **AC-M1.2a** `set -euo pipefail`，任何 kcadm/curl 失败即非零退出；**AC-M1.2b** 结尾自检：两个池文件行数 ≥ 期望、单发 client_credentials 得 200；**AC-M1.2c** 幂等：`down -v` 后重跑 setup 成功 |
| M1.3 | `scenarios/`（5 个 lua） | s1/s2/s3/s5/s6 按 §4.1 端点改写；S2 用 Basic（bench-svc）；S3 Basic + AT 池（线程切片，复用 s3 模式）；S5 RT one-shot 池；S6 Bearer AT 池 | **AC-M1.3** 每个场景 `wrk c=2 -d5s` 冒烟：非 2xx=0、socket 错误=0（S5 允许池尾 nil 关连接，但不得有 invalid_grant） |
| M1.4 | 阶梯数据 | `WARMUP_S=60 DURATION_S=10` 阶梯 2→128 × 5 场景 → 35 个 JSON；S2 档挂 `--observe-stats` 采 RSS | **AC-M1.4a** 35 JSON 全带 `product=keycloak` + 版本；**AC-M1.4b** 每档 driver CPU <80% 或 JSON 标 `limited=true`；**AC-M1.4c** RSS tsv 落盘且含 keycloak+postgres 行 |
| M1.5 | GC 抖动 | `run-gc-jitter.sh`：S6 c=32，30×10s 段（D6） | **AC-M1.5** JSON 含 30 个 P99 数据点 + 段起始时间戳；无段失败 |
| M1.6 | 冷启动 | 两模式：A=全新卷完整初始化（含 realm/client 建立）；B=预初始化卷仅重启 keycloak 容器 | **AC-M1.6** 2 个 JSON（mode A/B），含秒数与 RSS 峰值 |
| M1.7 | `teardown.sh` | down -v + 残留断言 | 同 AC-M1.1b |
| M1.8 | 首版两方对比 | AuthForge 同 session 重跑（§5.1）+ Keycloak 数据 → 草稿对比表 | **AC-M1.8** 5 场景 × {QPS, P50/P95/P99, RSS, 冷启动} 行齐、版本列齐 |

### M2 — Ory Hydra + Zitadel（~3 天）

**Hydra**：无内建用户体系。S5/S6 的用户 token 用官方 mock 模式 headless 驱动：`GET /oauth2/auth`（login 跳转）→ admin API `POST /admin/oauth2/auth/requests/login/accept` + `consent/accept` → code → token，纯 curl 可驱动（无需浏览器）。
**Zitadel**：S2 走 JWT profile 预签 assertion 池（D3 特例）；S3 用 API client + Basic；S5/S6 优先 v2 Session API 建 password 会话 → auth_code 换用户 token。

| # | 步骤 | 验收标准 |
|---|------|---------|
| M2.1 | Hydra compose（hydra v2 + PG，public 4444 / admin 4445）+ setup（client create + accept 流驱动签池）| 同 M1.1/M1.2 模式：探针 `/health/ready` 200；池文件行数自检；任何 curl/jq 失败非零退出 |
| M2.2 | Hydra scenarios + 阶梯 + gcjitter + 冷启动 | 同 M1.3–M1.6 验收；S3 JSON/结果带 admin-port 标注 |
| M2.3 | Zitadel compose（`setup` mode 初始化 + `start` mode 运行）+ setup（机器用户 JSON key、API client、人类用户） | 同 M1.1/M1.2；setup 两阶段（init/start）可分别重入 |
| M2.4 | Zitadel scenarios + 阶梯 + gcjitter + 冷启动 | 同 M1.3–M1.6；S2 结果注明 JWT-profile 认证等价性 |

**决策门（M2 内）**：
- **DG-1**：Hydra accept 流若 curl 驱动不成（如强制 JS），S5/S6 标 N/A。判据：setup 能稳定取得带 `openid` scope 的用户 token。
- **DG-2**：Zitadel Session API→auth_code 若 2 个工作日内驱动不成，S5/S6 标 N/A（S1/S2/S3 保底），限制小节写明。

### M3 — 汇总 + 诚实修订（~1 天）

| # | 步骤 | 验收标准 |
|---|------|---------|
| M3.1 | `gen-comparison.py` 聚合器 | 读四家 JSON 全自动生成 COMPARISON.md：主表（5 场景 × QPS/P50/P95/P99/RSS/冷启动 × 版本列）、GC 抖动小节、公平性附录（配置出处 + 偏离项 + warmup 差异）、限制小节（S4 排除、Ory admin-port、Zitadel JWT-profile、WSL2 声明）。**AC-M3.1**：无手填数字；缺某家某场景时显式 N/A 不缺行 |
| M3.2 | `run-comparison.sh` 编排器 | `--fresh` 全串行（每家之间清场+残留断言）+ `--only <product>` 补跑 + 末尾自动调聚合器。**AC-M3.2**：一条命令从空环境到 COMPARISON.md |
| M3.3 | research.md §3.1/§3.2 修订 | 竞品列改"同环境实测"；卖点按实测收敛。**AC-M3.3**：每个数字可溯源到入仓 JSON |
| M3.4 | 文档收尾 | benchmarks/README.md 增 competitors 指引；本文档 §六验收勾选 | AC1–AC6 全勾 |

---

## 八、目录结构设计（仅设计）

```
benchmarks/competitors/
├── README.md                      # 复现指引（环境/顺序/限制）
├── run-comparison.sh              # 串行跑全部（或 --only <product>）
├── run-gc-jitter.sh               # 5 分钟 P99 时间序列（D6）
├── keycloak/
│   ├── docker-compose.yml         # Keycloak + PG（对齐 D1）
│   ├── setup.sh                   # start --optimized + kcadm 初始化 + token 池
│   ├── teardown.sh
│   ├── lib/generated/             # setup 产物：token 池文件（WRK_LIB_DIR 指向此处）
│   └── scenarios/                 # s1/s2/s3/s5/s6.lua（端点按 §4.1）
├── ory/
│   ├── docker-compose.yml         # Hydra(+Kratos 如需) + PG
│   ├── setup.sh                   # hydra client create + token 池
│   ├── teardown.sh
│   └── scenarios/
├── zitadel/
│   ├── docker-compose.yml
│   ├── setup.sh                   # setup mode 初始化 + token 池
│   ├── teardown.sh
│   └── scenarios/
└── results/
    ├── <date>-<sha>-<product>-<scenario>-c<conn>.json   # 同 schema v1
    ├── <date>-<sha>-<product>-gcjitter.json
    └── COMPARISON.md              # 汇总对比表（gen-comparison.py 生成，M3）

# 聚合器放共享 reporting/（与 parse-wrk.py 同层）：
benchmarks/reporting/gen-comparison.py
```

**复用不复制**：`run-scenario.sh` / `parse-wrk.py` / `observe/` 直接引用 `benchmarks/authforge/` 与 `benchmarks/reporting/` 的现有实现（通过路径参数或环境变量），不为竞品复制第二份 runner。

---

## 九、风险与缓解

| 风险 | 等级 | 影响 | 缓解 |
|------|------|------|------|
| **竞品社区质疑配置不公平** | 高 | 对外数据被推翻，信誉受损 | D2 官方推荐配置 + AC4 全量标注偏离项；发布前可请竞品社区 review setup 脚本 |
| **Keycloak JIT/GC 预热不足，数字偏低被指不公平** | 高 | Keycloak 数字虚低 | warmup 延长 60s + 长跑前置 1 分钟丢弃段 |
| **Hydra 无内建用户，S6 不可测** | 中 | 场景覆盖缺口 | Hydra 配最小 login/consent mock app（官方 brownfield 模式）；若复杂度超预期，S6 对 Hydra 标 N/A |
| **S5 竞品池规模不足/重发太慢** | 中 | S5 数据失真或 setup 超时 | 校准跑定池规模（QPS×10s×1.3）；xargs -P8 并行签发；--reissue 每档重发（D5 v1.1） |
| **Zitadel token 端点不支持 Basic client_credentials** | 中 | S2 无法按统一 Basic 口径测 | JWT profile 预签 assertion 池（官方推荐路径，pyjwt 签发）；COMPARISON 注明认证等价性（D3 v1.1） |
| **Zitadel Session API→auth_code 驱动失败** | 中 | S5/S6 缺口 | 决策门 DG-2：2 个工作日不成标 N/A，S1/S2/S3 保底 |
| **竞品 token 池签发慢（2000 次 API 调用）** | 低 | setup 时间长 | 并行签发（xargs -P8）；或降池到 500（S3/S6 池可复用，量够） |
| **AuthForge 某维度不领先** | 中 | 卖点叙事受损 | 这正是设施价值——诚实收敛到领先维度（演进方案 §二原则 1 的既定预案） |
| **单机串行跑，环境漂移（系统更新/温度）** | 中 | 四家数据不同批不可比 | 同一 session 内连续跑完；每家结果带时间戳；复跑取中位数 |
| **wrk 打不满 Go/Java 服务（driver 受限）** | 低 | 数字是下限 | 沿用 AC4 driver CPU 门；超 80% 标注 |

---

## 附录 A：与上游文档的关系

| 上游 | 关系 |
|------|------|
| [基准设施设计](benchmark-facility-design.md) | 本文档是其 N1（Phase 0.5 竞品对比）的展开；复用其 runner/parser/schema/observe |
| [演进方案](../productization-evolution-plan.md) §三 Phase 0 P0 第 2 项 | 本文档是该工作项的落地设计 |
| [调研报告](../productization-research.md) §3.1 | 竞品列的**替换数据源**；M3 据实修订 |
| `benchmarks/results/SUMMARY.md` | AuthForge 侧基线（同环境自测，2026-08-12） |

## 附录 B：决策记录

| 决策 | 选择 | 备选与否决理由 |
|------|------|---------------|
| 对比对象 | Keycloak / Ory / Zitadel | Auth0（SaaS 不可自托管）否决 |
| 场景范围 | S1/S2/S3/S5/S6 | S4（登录流不可 headless 驱动）排除，详见 D4 |
| 配置基线 | 官方推荐生产配置 | 极限调优（军备竞赛无公信力）与默认 dev 配置（不公平）均否决 |
| token 池 | 各家 API 自签发 | SQL 直插（签名格式私有）否决，详见 D5 |
| 内存口径 | 容器全栈 RSS（主）+ PSS（补充） | 单一口径必有一方吃亏，双口径并列标注 |
| warmup/measure 口径（v1.1） | 5s/10s，Keycloak warmup 60s | 与入仓自测数据一致；原文"其余四家 10s"与实际数据（5s/10s）不符，已修正 |
| GC 抖动载波场景（v1.1） | S6 userinfo，c=32 固定 | S5（池耗尽）与 S2（AuthForge 每请求写库、负载构成不对称）否决，详见 D6 |
| AuthForge 数据口径（v1.1） | 同 session 重跑 | "复用旧数据"与 AC2（gcjitter 必采）+ R7（同 session 消漂移）矛盾，已修正 |
| S5 竞品池（v1.1） | 每档 API 重发（--reissue），池=QPS×10s×1.3 | 固定 2000 池（单发单耗会耗尽）否决；RT 不取自 client_credentials（RFC 6749 §4.4.3 禁止） |
