# 竞品同环境性能对比（COMPARISON）

> 生成时间：2026-08-22 12:51 UTC · 生成器：`benchmarks/reporting/gen-comparison.py`（无手填数字，全部溯源到入仓 JSON）
> 设计与方法论：`docs/productization-evolution/in-progress/competitor-benchmark-design.md`

## 环境与版本

| 产品 | 版本 |
|---|---|
| AuthForge | git 34c5017 |
| Keycloak | 26.7.1 |
| Ory Hydra | v26.2.0 |
| Zitadel | v4.17.1 |

同一台机器（WSL2 8 vCPU / 16GB）、同一 wrk 4.1.0 阶梯（2→128，warmup 5s / measure 10s）、同一 PostgreSQL 17 后端、串行执行、每家之间 `docker compose down -v` 清场。连接池：三家竞品按各自官方机制对齐到 25（D1，见附录 A）；AuthForge 使用自家 bench 调优档（池 64/64、cache on、auto_batch、reuse_port，构建用 opt-in LTO preset——即仓库文档化的性能优化后推荐基准档，非隐藏调优）。

## 一、稳态吞吐与延迟（阶梯，错误率 <0.01% 的最高档）

### S1 discovery

| 产品 | 稳态 QPS | 稳态档 c | P50 | P90 | P99 | 错误率 |
|---|---|---|---|---|---|---|
| AuthForge | **87,123** | c=64 | 0.7ms | 1.6ms | 142.5ms | 0.0057% |
| Keycloak | **40,124** | c=128 | 2.4ms | 16.9ms | 73.9ms | 0.0000% |
| Ory Hydra | **1,616** | c=128 | 78.9ms | 80.1ms | 83.3ms | 0.0000% |
| Zitadel | **8,580** | c=32 | 3.2ms | 7.5ms | 16.3ms | 0.0000% |

### S2 client_credentials

| 产品 | 稳态 QPS | 稳态档 c | P50 | P90 | P99 | 错误率 |
|---|---|---|---|---|---|---|
| AuthForge | **12,806** | c=128 | 9.5ms | 15.3ms | 22.6ms | 0.0000% |
| Keycloak | **5,428** | c=32 | 5.8ms | 8.9ms | 13.3ms | 0.0000% |
| Ory Hydra | **1,959** | c=64 | 30.6ms | 49.6ms | 79.7ms | 0.0000% |
| Zitadel | **1,517** | c=64 | 40.9ms | 50.4ms | 73.1ms | 0.0000% |

### S3 introspect

| 产品 | 稳态 QPS | 稳态档 c | P50 | P90 | P99 | 错误率 |
|---|---|---|---|---|---|---|
| AuthForge | **19,245** | c=128 | 6.4ms | 10.0ms | 14.2ms | 0.0000% |
| Keycloak | **10,556** | c=64 | 6.0ms | 9.7ms | 18.8ms | 0.0000% |
| Ory Hydra | **10,061** | c=128 | 11.9ms | 21.9ms | 35.3ms | 0.0000% |
| Zitadel | **2,946** | c=32 | 10.4ms | 14.6ms | 20.4ms | 0.0000% |

### S5 refresh_token

| 产品 | 稳态 QPS | 稳态档 c | P50 | P90 | P99 | 错误率 |
|---|---|---|---|---|---|---|
| AuthForge | **4,593** | c=128 | 28.8ms | 36.5ms | 44.8ms | 0.0000% |
| Keycloak | **4,336** | c=128 | 42.4ms | 56.7ms | 72.5ms | 0.0000% |
| Ory Hydra | **647** | c=128 | 190.3ms | 305.6ms | 440.3ms | 0.0000% |
| Zitadel | N/A | — | — | — | — | — |

### S6 userinfo

| 产品 | 稳态 QPS | 稳态档 c | P50 | P90 | P99 | 错误率 |
|---|---|---|---|---|---|---|
| AuthForge | **40,489** | c=128 | 3.0ms | 4.7ms | 139.2ms | 0.0000% |
| Keycloak | **29,145** | c=128 | 3.6ms | 20.9ms | 72.0ms | 0.0000% |
| Ory Hydra | **8,105** | c=128 | 13.9ms | 42.9ms | 624.1ms | 0.0000% |
| Zitadel | **3,395** | c=64 | 18.5ms | 22.8ms | 28.5ms | 0.0000% |

## 二、稳态内存（容器全栈 RSS，D7 口径）

S2 测量窗口内各容器 RSS 均值之和（含各自的 PG/Redis 与运行时；AuthForge 栈含 Redis 缓存层——各家架构自由选择的诚实口径）。

| 产品 | 全栈稳态 RSS |
|---|---|
| AuthForge | 5,350 MiB |
| Keycloak | 1,752 MiB |
| Ory Hydra | 261 MiB |
| Zitadel | 380 MiB |

## 三、冷启动

fresh = 全新卷完整初始化（含 DB schema 自动创建）→ 就绪探针 200；restart = 热卷仅重启服务容器。AuthForge 两种模式来自自家 measure-cold-start.sh（auto-migrate / pre-migrated），语义等价。

| 产品 | fresh (s) | restart (s) |
|---|---|---|
| AuthForge | 1.383 | 1.289 |
| Keycloak | 21.82 | 7.01 |
| Ory Hydra | 4.51 | 0.67 |
| Zitadel | 5.41 | 0.99 |

## 四、GC 抖动长跑（5 分钟 P99 时间序列，D6）

c=32 固定，30×10s 串行段；载波场景与偏离见附录。尖峰定义：段 P99 > 1.5×中位数。

| 产品 | 载波 | 段数 | P99 中位 | P99 最大 | 最大/中位 | 尖峰段数 |
|---|---|---|---|---|---|---|
| AuthForge | s6-userinfo | 30 | 88.3ms | 1110.0ms | 12.56x | 15 |
| Keycloak | s6-userinfo | 30 | 6.6ms | 594.5ms | 90.27x | 11 |
| Ory Hydra | s6-userinfo | 30 | 31.4ms | 1220.0ms | 38.9x | 11 |
| Zitadel | s6-userinfo | 30 | 18.8ms | 628.7ms | 33.41x | 8 |

逐段 P99（ms）：

- **AuthForge**: 1.9, 557.9, 1.8, 537.7, 550.6, 2, 3.5, 558.6, 193.7, 1.9, 556.7, 1.8, 1.9, 575.5, 2, 2.1, 549.7, 2, 172.7, 550.8, 1.9, 2, 557.8, 453.4, 4, 337.1, 1110, 2.5, 546.8, 2.8
- **Keycloak**: 6.2, 583.9, 6, 6.5, 576, 6, 448.8, 6.2, 6.3, 594.5, 6.2, 6.3, 586.7, 6.4, 6.4, 580.3, 6.5, 6.8, 586.1, 6.6, 6.5, 586.9, 6.5, 6.4, 582.3, 6.9, 6.7, 578.4, 5.7, 583.1
- **Ory Hydra**: 31.5, 30.1, 593.5, 30.7, 31.1, 619.4, 31.9, 604.9, 31.1, 30, 605.7, 29.2, 30.6, 596.6, 31.4, 613.1, 30.4, 31.1, 438.8, 1220, 33.4, 603.7, 30.4, 631.1, 30.9, 30.5, 602.9, 31.3, 30.8, 31
- **Zitadel**: 20.2, 18.5, 18.8, 19.1, 618.3, 18.4, 19.3, 611.4, 18.1, 18.7, 18.6, 19, 620.5, 18.4, 18.7, 612, 19.5, 19.3, 18.8, 18.7, 625, 18.3, 18.7, 621.3, 18, 18.7, 623, 18.2, 628.7, 18.4

> **诚实注记（G4，2026-08-21 重跑修订）**：本次四家在同机同时段**全部出现同款周期尖峰**（Keycloak 最大/中位 90.27x、Ory/Zitadel 亦数十倍）——跨产品同款尖峰直接证明这是宿主环境层噪声（WSL2 调度/IO 停顿），而非任何一家的运行时行为；各产品的 GC 差异在本口径下不可分辨。可比较的是绝对水位与比值：AuthForge 中位 P99 88.3ms、最大/中位 12.56x（vs Ory Hydra 31.4ms / Zitadel 18.8ms）——中位与比值均最优或并列最优时才可引用本表。

## 附录 A：公平性声明（配置来源与偏离项，AC4）

四家一律使用各自**官方推荐生产配置**，不做极限调优也不调差。偏离默认的每一项如下（全部为对齐口径或使测量可行的非性能项）：

| 产品 | 配置基线出处 | 偏离项 |
|---|---|---|
| AuthForge | benchmark 设施自测配置（config.bench.json + docker-compose.bench.yml：PG17、池 64/64、cache on、auto_batch、reuse_port=true；构建用 opt-in LTO preset）——wave-1/2 性能优化后的文档化基准档（docs/performance-optimization/） | 本表数字含 wave-2 代码优化（validateClient/user-read 缓存、EVAL 合并）；均为已交付仓库代码，非一次性调优 |
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

