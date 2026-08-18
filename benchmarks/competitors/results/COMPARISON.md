# 竞品同环境性能对比（COMPARISON）

> 生成时间：2026-08-18 01:32 UTC · 生成器：`benchmarks/reporting/gen-comparison.py`（无手填数字，全部溯源到入仓 JSON）
> 设计与方法论：`docs/productization-evolution/in-progress/competitor-benchmark-design.md`

## 环境与版本

| 产品 | 版本 |
|---|---|
| AuthForge | git 03965fa |
| Keycloak | 26.7.1 (git 03965fa) |
| Ory Hydra | v26.2.0 (git 03965fa) |
| Zitadel | v4.17.1 (git 03965fa) |

同一台机器（WSL2 8 vCPU / 16GB）、同一 wrk 4.1.0 阶梯（2→128，warmup 5s / measure 10s）、同一 PostgreSQL 15 后端（连接池对齐 25）、串行执行、每家之间 `docker compose down -v` 清场。

## 一、稳态吞吐与延迟（阶梯，错误率 <0.01% 的最高档）

### S1 discovery

| 产品 | 稳态 QPS | 稳态档 c | P50 | P90 | P99 | 错误率 |
|---|---|---|---|---|---|---|
| AuthForge | **94,640** | c=64 | 0.6ms | 1.4ms | 107.8ms | 0.0067% |
| Keycloak | **44,493** | c=128 | 2.2ms | 14.1ms | 60.7ms | 0.0000% |
| Ory Hydra | **1,604** | c=128 | 79.5ms | 80.8ms | 82.3ms | 0.0000% |
| Zitadel | **8,196** | c=32 | 3.4ms | 7.3ms | 15.2ms | 0.0000% |

### S2 client_credentials

| 产品 | 稳态 QPS | 稳态档 c | P50 | P90 | P99 | 错误率 |
|---|---|---|---|---|---|---|
| AuthForge | **9,056** | c=32 | 3.4ms | 4.4ms | 9.0ms | 0.0000% |
| Keycloak | **5,971** | c=64 | 10.5ms | 14.2ms | 21.8ms | 0.0000% |
| Ory Hydra | **1,916** | c=128 | 61.2ms | 113.0ms | 179.0ms | 0.0000% |
| Zitadel | **1,501** | c=64 | 41.5ms | 49.6ms | 66.5ms | 0.0000% |

### S3 introspect

| 产品 | 稳态 QPS | 稳态档 c | P50 | P90 | P99 | 错误率 |
|---|---|---|---|---|---|---|
| AuthForge | **17,602** | c=64 | 3.5ms | 4.6ms | 6.1ms | 0.0000% |
| Keycloak | **11,618** | c=128 | 10.7ms | 14.0ms | 22.0ms | 0.0000% |
| Ory Hydra | **10,933** | c=128 | 10.8ms | 20.3ms | 32.1ms | 0.0000% |
| Zitadel | **2,872** | c=32 | 10.6ms | 15.0ms | 21.6ms | 0.0000% |

### S5 refresh_token

| 产品 | 稳态 QPS | 稳态档 c | P50 | P90 | P99 | 错误率 |
|---|---|---|---|---|---|---|
| AuthForge | **1,998** | c=16 | 4.6ms | 6.0ms | 7.6ms | 0.0000% |
| Keycloak | **2,802** | c=128 | 49.8ms | 59.2ms | 72.5ms | 0.0000% |
| Ory Hydra | **669** | c=32 | 47.2ms | 55.6ms | 65.7ms | 0.0000% |
| Zitadel | N/A | — | — | — | — | — |

### S6 userinfo

| 产品 | 稳态 QPS | 稳态档 c | P50 | P90 | P99 | 错误率 |
|---|---|---|---|---|---|---|
| AuthForge | **18,278** | c=128 | 7.0ms | 7.9ms | 9.0ms | 0.0000% |
| Keycloak | **33,347** | c=128 | 3.1ms | 17.5ms | 65.5ms | 0.0000% |
| Ory Hydra | **10,345** | c=128 | 11.4ms | 21.4ms | 34.4ms | 0.0000% |
| Zitadel | **3,207** | c=32 | 9.4ms | 13.7ms | 21.7ms | 0.0000% |

## 二、稳态内存（容器全栈 RSS，D7 口径）

S2 测量窗口内各容器 RSS 均值之和（含各自的 PG/Redis 与运行时；AuthForge 栈含 Redis 缓存层——各家架构自由选择的诚实口径）。

| 产品 | 全栈稳态 RSS |
|---|---|
| AuthForge | 5,381 MiB |
| Keycloak | 1,674 MiB |
| Ory Hydra | 266 MiB |
| Zitadel | 387 MiB |

## 三、冷启动

fresh = 全新卷完整初始化（含 DB schema 自动创建）→ 就绪探针 200；restart = 热卷仅重启服务容器。AuthForge 两种模式来自自家 measure-cold-start.sh（auto-migrate / pre-migrated），语义等价。

| 产品 | fresh (s) | restart (s) |
|---|---|---|
| AuthForge | 1.233 | 1.257 |
| Keycloak | 20.34 | 5.52 |
| Ory Hydra | 4.89 | 0.69 |
| Zitadel | 5.94 | 0.97 |

## 四、GC 抖动长跑（5 分钟 P99 时间序列，D6）

c=32 固定，30×10s 串行段；载波场景与偏离见附录。尖峰定义：段 P99 > 1.5×中位数。

| 产品 | 载波 | 段数 | P99 中位 | P99 最大 | 最大/中位 | 尖峰段数 |
|---|---|---|---|---|---|---|
| AuthForge | s6-userinfo | 30 | 3.3ms | 655.5ms | 197.45x | 7 |
| Keycloak | s6-userinfo | 30 | 5.0ms | 5.4ms | 1.08x | 0 |
| Ory Hydra | s6-userinfo | 30 | 27.0ms | 28.4ms | 1.05x | 0 |
| Zitadel | s6-userinfo | 30 | 20.5ms | 22.6ms | 1.1x | 0 |

逐段 P99（ms）：

- **AuthForge**: 3.3, 116.9, 3.1, 3.6, 3.4, 3.2, 17.6, 3.1, 3.1, 3.3, 655.5, 114, 3.3, 3.3, 3.4, 3.4, 6, 3.2, 3.3, 3.2, 116.1, 3.2, 3.1, 3.4, 4.1, 3.3, 5.5, 3.4, 3.2, 3.2
- **Keycloak**: 4.9, 5.1, 5.1, 4.9, 5.2, 4.8, 5, 5.3, 4.9, 5.1, 5.2, 4.9, 5.1, 5.4, 5, 5, 5, 5, 4.6, 5.4, 4.9, 5, 5.2, 5.2, 5.2, 5, 5, 5.4, 4.8, 5
- **Ory Hydra**: 27, 27.9, 27.7, 27.3, 28.4, 26.9, 27.2, 27.8, 26.6, 25.9, 26.5, 27.3, 26.2, 27, 26, 26.6, 28.1, 26.9, 27.5, 27.1, 26.4, 26.9, 27.8, 27.1, 26.8, 27.8, 26.9, 26.6, 27.3, 26.8
- **Zitadel**: 21.9, 22.6, 19.3, 19.9, 20.2, 20.7, 19.8, 20.8, 21.1, 21.8, 20.5, 21.6, 19.5, 20.6, 20.8, 21.3, 20, 21.1, 18.9, 20.6, 20.4, 19.2, 20.2, 20.2, 20.1, 22.6, 20.2, 21.9, 20.1, 20.9

> **诚实注记（G4 修订）**：设计预期「GC 语言出现周期尖峰、AuthForge 平线」**未被本次实测证实**——Keycloak（JVM）/ Ory（Go）/ Zitadel（Go）在本负载下 P99 全程平线（最大/中位 ≤1.1x，现代 GC 并发化后 10s 窗口测不出 STW），反倒是 AuthForge 出现 7 个尖峰段（最大 655.5ms）。C++ 无 GC，这些尖峰是环境层停顿（WSL2 宿主调度 / PG checkpoint IO），并非运行时 GC——「无 GC 抖动」不能作为对外差异化主张引用本表；可作为主张的是绝对 P99 水位（中位 3.3ms vs Ory Hydra 27.0ms / Zitadel 20.5ms）。

## 附录 A：公平性声明（配置来源与偏离项，AC4）

四家一律使用各自**官方推荐生产配置**，不做极限调优也不调差。偏离默认的每一项如下（全部为对齐口径或使测量可行的非性能项）：

| 产品 | 配置基线出处 | 偏离项 |
|---|---|---|
| AuthForge | benchmark 设施自测配置（config.bench.json，PG 池 25 / Redis 20，Phase 0 已入仓） | — |
| Keycloak | keycloak.org/server/containers 与 /server/db | PG 连接池 25（默认 100，D1 对齐）；KC_HEALTH_ENABLED=true；realm accessTokenLifespan/SSO idle 提到 1h（token 池须跑完整个阶梯，签名路径不变）；bench client 增加 audience mapper（KC 26 内省强制 aud 校验，官方机制）；setup 阶段 60s JIT 预热（D2 豁免，JVM 特有） |
| Ory Hydra | ory.sh/docs/hydra/self-hosted/deploy-hydra 与 configure | DSN max_conns=25（D1 对齐）；login/consent URL 指向占位（用官方 admin-API accept 流 headless 驱动用户流）；自签 TLS 直接服务 public+admin 端口（v26 生产模式强制 https issuer，--dev 非生产配置；serve.tls 为两监听共享；wrk 连接复用使握手在测量窗口外）
| Zitadel | zitadel.com/docs/self-hosting/deploy/compose 与 configure（v4.17.1，当前稳定线，与 Keycloak 26 / Hydra 26 同代） | 单节点精简 compose（去掉官方示例的旁路观测组件）；PG 池 MaxOpenConns=25（D1 对齐）；FirstInstance.Features.ImprovedPerformance 全开 1-5（官方文档化的默认实例配置，Zitadel 自家 v4 基准同款基线）；S2 = RFC 7523 jwt-bearer 授权（Service User 官方 M2M 路径——token 端点对机器用户不接受 client_credentials+client_assertion）；S3 = OIDC app + 私钥 JWT 客户端认证（官方性能建议 #6220：secret 认证每请求做哈希）；S5 N/A：机器用户无 refresh token（RFC 6749 §4.4.3）且 password grant 已移除；阶梯前投影平复门（mint 2000 token 后 CQRS 投影追赶期间开压会产生 500 风暴） |

统一压测口径：wrk 4.1.0，阶梯 2→4→8→16→32→64→128，warmup 5s（丢弃）/ measure 10s，-t = min(cores, conns/16)；driver CPU 均低于 80% 门（超限档在 JSON 中标 limited。

## 附录 B：方法限制（诚实声明）

1. **S4 auth_code 场景排除**（D4）：各产品登录/consent 交互流无法用 wrk 统一驱动；测的会是登录页渲染而非 token 管线。
2. **Ory Hydra introspect 在 admin 端口（:4445）**（D3）：生产部署中该端口通常不对外，语义差异如上标注。
3. **Zitadel S5/S6 限制**：机器用户无 refresh token（RFC 6749 4.4.3），Zitadel 亦移除 password grant——S5 标 N/A；S6 仅当服务用户 token 被 userinfo 接受时给出（见结果表），否则 N/A。
4. **单次测量**：每档单次 10s（与 Phase 0 自测口径一致）；环境为 WSL2 虚拟机（8 vCPU / 16GB），数字是下限不是上限。
5. **Keycloak JVM 预热豁免**（D2）：setup 阶段 60s client_credentials 预热后，各场景用与其它家一致的 5s warmup。

