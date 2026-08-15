# AuthForge 竞品性能基准对比设计

> **版本**: v1.0
> **日期**: 2026-08-15
> **文档性质**: 技术设计（Phase 0.5 落地蓝图，**非代码**——实施见 §七 milestone）
> **上游规划**: [演进方案 §三 Phase 0](../productization-evolution-plan.md) P0「自托管竞品对比基准」
> **前置依赖**: [基准设施设计](benchmark-facility-design.md) M1–M4 已交付（S1–S6 自测数据已入仓 `benchmarks/results/`）
> **验证对象**: [调研报告 §3.1](../productization-research.md) 竞品列（Keycloak/Ory）的量级参考数字

---

## 零、TL;DR

- **做什么**：在**同一台机器、同一套 wrk 阶梯、同一个 PostgreSQL 后端**下压 Keycloak / Ory Hydra / Zitadel，与 AuthForge 已入仓的自测数据（`benchmarks/results/SUMMARY.md`）产出同口径对比表。
- **比什么**：S1 discovery / S2 client_credentials / S5 refresh_token / S6 userinfo 四个单步场景 + 冷启动 + 稳态 RSS + **GC 抖动长跑**（5 分钟 P99 时间序列——AuthForge 无 GC 的核心差异化证据）。
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
| N5 | AuthForge 自测场景重跑 | 复用已入仓的 `benchmarks/results/`（同环境同参数），除非环境变更需重测全部四家 |

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
| S5 refresh_token | 1,982 | 26ms | 0.000% |
| S6 userinfo | 16,674 | 12ms | 0.000% |

环境：WSL2 8 vCPU / 16GB / PG 连接池 25 / Redis 20 / wrk 4.1.0 / 阶梯 2→128。
详见 `benchmarks/results/SUMMARY.md`。

### 2.3 现有可复用资产

| 资产 | 复用方式 |
|------|---------|
| `run-scenario.sh` 阶梯 runner（warmup→measure→JSON） | 竞品场景直接传不同的 `.lua`，runner 不变 |
| `parse-wrk.py`（wrk 文本→schema v1 JSON） | 场景无关，零改动 |
| `observe/docker-stats.sh` + `scrape-metrics.sh` | 容器 RSS/CPU 采样（竞品容器名不同，参数化） |
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
| 压测工具 | wrk 4.1.0，同一份阶梯参数（2→4→8→16→32→64→128）、warmup/measure 时长 | 复用 `run-scenario.sh`，不写第二套 runner |
| 后端存储 | PostgreSQL 15（同一 image tag），连接池对齐 25 | 竞品各自支持的连接池上限可能 <25，取 `min(25, 官方上限)` 并在结果中标注 |
| 网络拓扑 | localhost cross-container（wrk 在宿主机） | 与自测一致 |
| 结果格式 | schema v1 JSON（`parse-wrk.py`） | 四家数据同构，`run-comparison.sh` 才能聚合 |

### D2 — 竞品配置 = 官方推荐生产配置（不调优、不调差）

**依据**：极限调优对比无公信力；故意调差是学术造假。

| 竞品 | 配置基线 | 官方出处 |
|------|---------|---------|
| Keycloak | `start --optimized` + PostgreSQL，内存按官方建议 2GB 限额 | [Running Keycloak in a container](https://www.keycloak.org/server/containers) |
| Ory Hydra | 官方 docker-compose + PostgreSQL DSN | [Ory Hydra docs](https://www.ory.sh/docs/hydra/self-hosted/deploy-hydra) |
| Zitadel | 官方 compose（`setup mode` 初始化 + PostgreSQL） | [Set up ZITADEL with Docker Compose](https://zitadel.com/docs/self-hosting/deploy/compose) |

每家竞品的 `setup.sh` 头部注释必须附官方文档链接；偏离官方默认的每一项（如连接池对齐）单独注释理由。

**JVM 预热豁免**：Keycloak 的 JIT/GC 需要更长预热。warmup 对 Keycloak 延长到 60s（其余四家 10s），在结果中标注——这不是偏袒，是给 JIT 编译时间，否则测的是"未编译的解释执行"。

### D3 — 场景映射：功能等价，不是路径等价

各产品端点路径不同，映射到**同一功能**的端点（§四矩阵）。关键约束：

- **S3 introspect 的 Ory 特例**：Hydra 的 introspect 在 admin 端口（4445）而非 public 端口，且生产部署中 admin 端口通常不对外。为公平，四家都测 introspect 但**在 COMPARISON.md 中标注 Ory 的 admin-port 语义差异**。
- **认证方式**：client_credentials 用各产品的标准 client 认证（Basic 或 post，按其声明）。

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

**方案**：每家竞品的 `setup.sh` 通过其**自身的 client_credentials 端点**批量签发 token（N 次 token 请求），把活跃 token 写入 `access_tokens.txt`，Lua 场景脚本复用同一套 token-pool 逻辑。签发 N=2000 个 token 本身约需 1–2 分钟，一次性成本可接受。

### D6 — GC 抖动 = 长跑 P99 时间序列（AuthForge 核心差异化证据）

单次 30s 跑看不出 GC 周期。设计专门的**长跑测试**：

```
每家：c=32（或各家稳态拐点），持续 5 分钟
采集：每 10s 窗口记录一次该窗口的 P99（wrk 不支持原生分段 → 用 30 个串行 10s 段近似，或 5 个串行 60s 段）
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
1. AuthForge    — 复用已入仓数据（环境未变时不重跑）
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

| # | 验收项 | 衡量 |
|---|--------|------|
| AC1 | **四家同口径对比表**：S1/S2/S3/S5/S6 × {QPS, P99, RSS, 冷启动} 入仓 `benchmarks/competitors/results/COMPARISON.md`，每行带产品版本号 | COMPARISON.md |
| AC2 | **GC 抖动曲线**：四家 5 分钟 P99 时间序列 JSON + 对比小节（AuthForge 是否平线、Keycloak 是否有周期尖峰） | gcjitter JSON + 小节 |
| AC3 | **可复现**：`run-comparison.sh` 一键串行跑四家；README 含环境要求与复现步骤 | 复现指引 |
| AC4 | **公平性声明**：每家竞品的配置来源（官方文档链接）、偏离默认的每一项、warmup 差异（Keycloak 60s）均显式标注 | COMPARISON.md 附录 + setup.sh 注释 |
| AC5 | **诚实修订**：调研报告 §3.1 竞品列更新为"同环境实测"，§3.2 卖点若被证伪则收敛 | research.md 更新 |
| AC6 | **S4 排除声明**：COMPARISON.md 注明 auth_code 场景的方法限制 | 限制小节 |

---

## 七、实施计划（3 个 milestone）

### M1 — Keycloak 对比（最重，先啃硬骨头）

- 做：`competitors/keycloak/`（compose + setup + 4 个 Lua）+ 长跑脚本 + 与 AuthForge 已入仓数据拼出首版两方对比表
- 难点：realm/client 的 headless 初始化（`kcadm.sh`）；JVM warmup 校准
- 验收：Keycloak 五场景数据入仓；AC4 公平性标注齐

### M2 — Ory Hydra + Zitadel

- 做：两家 setup + 场景适配（Hydra 双端口、token 自签发池）
- 难点：Hydra 无内建用户体系（S6 userinfo 需配合 OIDC 模式的 token）；Zitadel instance 初始化
- 验收：AC1 四家齐

### M3 — 汇总 + 诚实修订

- 做：`run-comparison.sh` 聚合器 + COMPARISON.md 生成 + GC 抖动对比小节 + research.md §3.1/§3.2 修订
- 验收：AC3 + AC5

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
    └── COMPARISON.md              # 汇总对比表（M3 生成）
```

**复用不复制**：`run-scenario.sh` / `parse-wrk.py` / `observe/` 直接引用 `benchmarks/authforge/` 与 `benchmarks/reporting/` 的现有实现（通过路径参数或环境变量），不为竞品复制第二份 runner。

---

## 九、风险与缓解

| 风险 | 等级 | 影响 | 缓解 |
|------|------|------|------|
| **竞品社区质疑配置不公平** | 高 | 对外数据被推翻，信誉受损 | D2 官方推荐配置 + AC4 全量标注偏离项；发布前可请竞品社区 review setup 脚本 |
| **Keycloak JIT/GC 预热不足，数字偏低被指不公平** | 高 | Keycloak 数字虚低 | warmup 延长 60s + 长跑前置 1 分钟丢弃段 |
| **Hydra 无内建用户，S6 不可测** | 中 | 场景覆盖缺口 | Hydra 配最小 login/consent mock app（官方 brownfield 模式）；若复杂度超预期，S6 对 Hydra 标 N/A |
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
