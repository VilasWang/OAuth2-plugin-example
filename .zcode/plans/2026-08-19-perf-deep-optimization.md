# 深度性能优化实施计划（2026-08-19）

依据：`docs/performance-optimization-prompt.md`（下称"提示词文档"）
分支：`feat/perf-deep-optimization`（自 `feat/competitor-benchmark` 切出，PR 以其为 base 堆叠）

---

## 一、提示词文档合理性评估

### 结论：方法论合理，状态快照过时；按实际状态校准后执行，不推翻框架

提示词文档的核心方法论与项目既有 SOP（noncode-performance-optimization.md 的 A/B 实践）一致且更严格：
证据优先、一次一个变量、量化验收阈值（QPS≥+5% 且 P99 不劣化 >5%，或 P99 改善≥10%）、
基线归档、perf 降级方案。全部采纳。

**过时/失实之处（已对照分支实际状态核实）：**

| # | 提示词文档的说法 | 实际状态 | 处置 |
|---|---|---|---|
| 1 | "当前默认配置中缓存是关闭的" | bench 自 8838ac6 起 cache.enabled=true（client TTL 300s / token 60s）；dev/prod 仍 false | 分析按 bench 实际为准 |
| 2 | 基准值 PG 25 / Redis 20 | 自 a9d6327 起 bench 池为 64/64（100 已实测无增益而否决） | 同上 |
| 3 | "CachedOAuth2Storage 提供 L1 内存缓存" | 该类不存在；实际是 RedisCachedClientRepository / RedisCachedTokenRepository（L2 Redis）；CachedClientRepository（L1）存在但从未接线 | 按实际类名分析 |
| 4 | PostgreSQL 14+ | bench 与 deploy 已 PG17（Helm 仍 15，待升级手册） | 记录为遗留项 |
| 5 | 阶段一步骤 B 需跑基线 | 文档自身允许"已有最新结果可不做"；`20260819-97c9254` 全量基线（6 场景 × 8 级）昨天刚产出 | 复用，不重跑；补归档 |
| 6 | 步骤 D 假设可容器内 perf | 本机 Docker Desktop WSL2，perf_event_paranoid 通常阻断 | 按文档降级方案，timebox |
| 7 | "禁止 git push" | 本次任务要求发 PR（用户指令优先）；推送走 SSH（本机 HTTPS 403） | 按用户指令，推送前完成本地全部门禁 |

**与既有工作的关系（避免重复劳动）：**
已完成：cache-on、池 64/64、PG17+GUC 调优、V025 audit 分区、V026 冗余索引清理、auto_batch=true、
基线 20260819-97c9254。提示词文档阶段一的大部分（基线/DB 分析）已由前序会话完成。
**真实剩余缺口**：编译档位（LTO/PGO 全未设）、缓存 TTL stampede（实测 30s 周期 ~800ms p99 尖峰）、
is_fast 等代码级积压（§十，明确保持 deferred）、性能回归门禁缺失、benchmarks/baseline/ 归档约定未落地。

### 明确不做（本轮否决/延后，附理由）
- **is_fast db/redis client**：config+代码双改，Redis 陷阱（连接变为每 IO 线程一份），既有结论 keep deferred
- **PGO**：跨编译器 profile 流程复杂，收益不确定，先 LTO 后评估
- **-march=native / x86-64-v3**：IO 密集型服务预期收益小、可移植性风险真实，不做
- **-ffast-math**：文档自身风险提示成立，不启用
- **prod/dev cache-on**：部署正确性决策（撤销语义），本轮只出建议不改配置
- **CI 性能回归门禁**：GitHub hosted runner 跑不出可信数据（噪声），需专用 runner/裸机，延后
- **L1 CachedClientRepository 接线**：类存在但从未接线；L1 失效语义未设计（Redis 装饰器自身尚无 client 更新/删除失效），叠 L1 会放大该缺口
- **审计采样/批量、JWT ES256、auto_batch 读写分离**：§十积压，涉及行为语义，单独任务

---

## 二、实施阶段与验收标准

### P0 — CI 静态检查修复（阻塞项，最先做）
**根因**：PR #64 的 CI "Static Checks" 失败于 migration-check 规则 M5 ——
V025/V026 迁移已提交但 `tools/migration-check/baseline.json` 未更新（程序性遗漏，本地已复现）。
两个迁移内容已复查：幂等守卫齐全（V025 顶层 DO 块整体跳过 + 对象级 IF NOT EXISTS；V026 纯 DROP INDEX IF EXISTS）。

步骤：
1. `python tools/migration-check/migration_check.py --update-baseline`，提交到 `feat/competitor-benchmark`（V025/V026 在该分支引入，修复归属同处）；[评审采纳] --update-baseline 会从全部当前文件重生成，提交前 `git diff tools/migration-check/baseline.json` 确认**只新增 V025/V026 两行**（防止顺带掩盖 V001-V024 的并发篡改）
2. 本地全量核实 CI 静态检查 7 项（对应 ci.yml static-checks job）：
   arch-guard / migration-check / api-diff / naming_validator / manage-parity / openapi-spec-validator / check_spec_governance
   [评审采纳] Windows 本地注意：CI 用 `python3` 本地用 `python`；openapi-spec-validator 需本地 `pip install openapi-spec-validator pyyaml`；naming_validator 的 `grep -zoP` 在 Git Bash GNU grep 可用；迁移哈希已做 LF 归一化，行尾无碍

**验收**：7 项全部本地通过（exit 0）。推送后 PR #64 的 static-checks 转绿（build-test/frontend 门此前已绿，以推送后实际为准，若另有红项另行处理）。

### P1 — 阶段一：分析报告（复用既有证据 + 补缺口）
1. **基线归档**（提示词规则 2）：`mkdir benchmarks/baseline && cp benchmarks/results/20260819-97c9254-*.json benchmarks/baseline/` + README 说明约定
   [评审修正] 该基线实际只含 **S1/S2/S3/S5/S6（5 场景 × 8 级）**，无 S4（S4 仅有 20260812-1702246 旧组，6 级无 c128）；README 必须如实标注。实测参数为每级 5s 预热 + 10s 测量（非提示词所说的 10s+30s），后续 A/B 沿用实测参数保证可比。
2. **既有结果分析**：解析脚本**复用** `benchmarks/reporting/gen-comparison.py` 的语义（`staircase_map` 68-90 行的 (date,sha) 组选择、`steady` 93-103 行的最高 QPS + error_rate<0.01% 门槛、driver-limited 门 348-349 行），**显式 pin 97c9254 组**（新结果落入 results/ 会静默切换比较组）；产出每场景 knee point、S1/S2/S3 场景间 QPS 比、P99 斜率、错误率、driver.limited 标注。S4 分析引用 20260812 旧组并标注跨周方向性
3. **S4 补基线**：bench 栈就绪后同日补跑一次 S4（全阶梯），为 P2 的 A/B 提供当日参照（~7 分钟）
3. **静态热点扫描**（提示词步骤 A，聚焦未覆盖面）：S4/S5 多步链的回调串行 RTT、TokenService/JwkManager、控制器 JSON 拷贝、日志在热路径的分布；输出可疑点清单（文件:行 + 证据）
4. **EXPLAIN ANALYZE**（提示词步骤 E + V026 遗留项）：token 点查（S3/S6）、client 查找（S2）、user 查找（S4）；bench 栈起来后 docker exec psql 执行
5. **CPU 热点**（提示词步骤 D，timebox 30min）：容器内 perf 若被权限阻断 → 按文档降级 gdb 采样；再不行则记为环境受限，以步骤 2/3/4 证据为准

**验收**：产出 `docs/performance-optimization-report.md`，含数据驱动瓶颈表（每项附 QPS/perf/EXPLAIN/代码行号证据、影响场景、预估提升+置信度、难度），按"预估提升×影响场景数"排序；无证据项只能进"待验证"附录。归档目录存在且含基线 JSON。

### P2 — 阶段二：优化实施（逐项：单变量提交 → 同日 A/B → 规则 3 判定）

**P2.1 缓存 TTL jitter（证据：§十实测 30s 周期 ~800ms p99 尖峰，gcjitter 长跑时序）**
- 改动：`libs/storage-redis/src/RedisCachedTokenRepository.cc`（3 处正缓存 SET：getAccessToken fill ~L223 / introspectToken fill ~L339 / saveAccessToken warm ~L458）+ `RedisCachedClientRepository.cc`（1 处 client fill ~L171）
- 实现[评审采纳]：纯函数 `applyTtlJitter(int ttl) -> int`（放 src 内部共享头，**不放 public include** —— api-diff 快照 libs/*/include/authforge，新增公共头会触发漂移）；`thread_local` 引擎（回调跑在 IO 线程，共享 static mt19937 是数据竞争）；仓库内 RNG 先例：OpenSSL RAND_bytes + random_device 回退（LogoutToken.cc:16-29）
- 语义：`ttl_final = ttl - uniform_int(0, ceil(ttl * 0.15))`；**只减不增**（C7 守卫不变）；ttl==1 直通；负缓存 SET 60（L405-410）不动（revoke 事件天然不对齐）；storage_type=redis 的 SETEX 站点不动（bench 不用且 TTL 承载过期语义）
- 单测[评审采纳]：`tests/integration/storage/` 既有模式（`Integration_P2_Storage_...` 命名，naming_validator 要求 `Category_P[0-3]_` 前缀，GLOB_RECURSE 免改 CMake）；确定性边界断言（0 ≤ 减量 ≤ ceil(0.15·ttl)、ttl_final ≥ 1、ttl==1/2 特例）+ 宽松分桶占位断言（**不用固定 alpha 卡方** —— 会以 alpha 概率误杀正确代码）；注意现有 Redis 集成测试无活 Redis 时 skip，本地 Windows ctest 需 Redis 在跑
- A/B[评审重锚]：**主证据 = gcjitter 长跑**（`benchmarks/competitors/run-gc-jitter.sh`，s6 载体 30 段，正是产出尖峰证据的仪器）改动前后同日各一次，看 30s 周期尖峰幅值；**次证据 = S3/S6 阶梯**（10s 窗口随机覆盖尖峰，仅作参考）
- **验收**：gcjitter p99 尖峰幅值显著收敛（目标：尖峰段 p99 / 中位比值下降 ≥30%）；阶梯按规则 3；若阶梯落噪声内属预期（如实标注），判定以 gcjitter 为准

**P2.2 LTO Release 档位（维度 1 缺口，项目自身 Step 3 的一半）**
[评审 BLOCKER 修正] 原设计"arm=LTO"无法执行：bench 镜像经 `deploy/docker/Dockerfile:46` → `scripts/backend/build.sh:78` → `env_common.sh:53-54` 在 Linux 上**无条件**解析为 `linux-release` preset —— 新增 opt-in preset 根本进不了被测二进制。修正为：
- 改动集：
  1. 根 CMake：`cmake/Lto.cmake`（`include(CheckIPOSupported)` + 守护），option `AUTHFORGE_ENABLE_LTO` **默认 OFF**（默认 preset 行为字节级不变，CI 零影响）
  2. `CMakePresets.json`：`linux-release-lto` / `windows-msvc-lto`，按 asan/tsan 模式覆写 `binaryDir` + conan `--output-folder` 匹配 + build preset 带 `configuration: Release`；condition 继承自带平台条件
  3. **构建管道打通**：`scripts/backend/env_common.sh` 的 `resolve_cmake_preset` 支持 env 覆盖（如 `AUTHFORGE_CMAKE_PRESET`），`Dockerfile` 加 ARG 透传，bench compose 构建参数可选传入 —— 使 bench 镜像真正能按 LTO 档构建
  4. 若 3 的管道改动超出合理范围（跨脚本连锁），降级方案：LTO 档位只做 build+ctest 验证（MSVC 本地 + 结论如实标注"未做吞吐 A/B"），不虚构 A/B 数据
- 本地验证：windows-msvc-lto 构建成功 + ctest 全过（验证编译性，bench 二进制是 Linux GCC —— 治理声明中区分）
- A/B：管道打通前提下，同日 arm=默认 Release vs arm=LTO：S2（DB 密集）+ S1（纯 CPU 框架天花板，最可能显 LTO）+ S4（当日补的基线参照）；两 arm 用 `RESULTS_DIR` 隔离（若同 sha 构建配置不同会导致文件名碰撞）
- **验收**：构建+测试全过为硬门槛；规则 3 判定；未达阈值 → 保留 opt-in 档位 + 报告如实标注；预期管理：IO 密集服务 LTO 典型增益 0-3%，可能低于噪声 floor

**P2.3 reuse_port A/B（维度 6 遗留项，纯配置，stretch）**
- 仅当 P2.1/P2.2 时间富余：bench config reuse_port true vs false 同日 A/B（S2）
- [评审采纳] 配置翻转不产生新 sha → 两 arm 结果文件同名互覆；**必须**用 `RESULTS_DIR` env（run-scenario.sh:66）按 arm 隔离
- **验收**：规则 3；无增益则记录否决

**每项共同验收**：一次提交一个变量；A/B 同日背靠背跑；对比表用提示词规则 2 格式；bench 与 full_test 严格串行（本机 contention 陷阱）

### P3 — 全量验证
- 顺序：先 bench A/B 全部完成 → 再 full-test（串行铁律）
- 后端 8 步（full-backend-test skill：DB 重置→ORM→构建→ctest→服务器→59 OAuth2→52 Admin→关闭）8/8
- 前端全量（admin 16 e2e + user 8 e2e + 单元 5 文件）
- CI 静态检查 7 项再跑一遍

**验收**：全绿；失败则修复后重跑（不留红）。

### P4 — 治理面评估
- **OpenAPI**：无端点/语义变化 → 预期不改；用 check_spec_governance 本地证实
- **SDK API 面**：P2 改动均在 .cc 内部、无头文件变化 → api-diff 应零漂移；若意外漂移按其规则处理
- **版本号**：纯性能/构建改动，无 API/行为契约变化 → 不 bump（info.version==Version.cmake 同步门禁不受扰动）
- **文档**：noncode-performance-optimization.md §十（stampede 项标记完成）、Step 3（LTO 状态）；新增 performance-optimization-report.md；benchmarks/baseline/README

### P5 — PR 与评审
1. CI 修复提交已在 feat/competitor-benchmark（P0）
2. feat/perf-deep-optimization PR，base=feat/competitor-benchmark（堆叠，diff 干净）
   [评审采纳] ci.yml 的 pull_request 触发器限定 `branches: ["master"]` —— **堆叠 PR 不会跑任何 CI**。验收改为：推送前本地 7 项静态检查 + full-test 全绿作为门禁；#64 合并后 PR 改 base 到 master 触发真实 CI，转绿后再请人工评审合并
3. 自评 + code-reviewer 子代理评审（正确性/规范/并发/回归风险），高置信问题全部解决后才算完成
4. 推送走 SSH（HTTPS 403），force-with-lease 仅在需要时

**验收**：本地门禁全绿；评审意见清零或降级为已记录的 follow-up；PR 描述注明"合并 #64 后改 base 触发 CI"。

---

## 三、风险与对策
| 风险 | 对策 |
|---|---|
| 本机 bench 与 full_test 资源互抢 | 严格串行；bench 前查 Redis 僵尸/5555 占用 |
| WSL 第二克隆漂移 | bench 一律从 D:\ 本克隆驱动，不碰 WSL 侧 |
| 跨天方差（S3/S6 已知） | A/B 只做同日背靠背；结论只引用同日对 |
| MSVC LTO 链接时间暴涨 | 只影响 opt-in preset；默认档不动 |
| TTL jitter 引入正确性风险 | 只减不增、边界单测、负缓存不动、C7 守卫保留 |
| dump.rdb 仓库根目录未跟踪文件 | 不提交、不删除（疑似 Redis 容器遗留），PR 里说明 |

## 四、执行顺序总览
P0（CI 修复+本地静态核实）→ P1（分析报告+归档）→ P2.1 → P2.2 → (P2.3) → P3（full-test 全量）→ P4（治理）→ P5（PR+评审）
