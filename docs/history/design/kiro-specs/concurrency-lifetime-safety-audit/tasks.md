# Implementation Plan

> 实施计划（并发与生命周期安全审计 Bugfix）

## Overview

本文件是 `design.md`（已多轮评审通过）的**实现阶段任务清单**。与 `bugfix.md`/`design.md` 仅做"分析 + 计划"不同，**本文件允许包含改动源码的步骤**——这正是 implement 阶段的预期。

方法论：每个修复遵循**缺陷条件（Bug Condition）两阶段**流程——
1. **复现（Explore）**：先在**未修复代码（F）**上写会失败的复现测试（TSan/ASan/快照），确认根因并取得反例；
2. **修复（Implement）**：按 `design.md` 的 Fix Implementation 落地；
3. **验证（Verify）**：在**修复后代码（F')**上重跑——sanitizer 干净（Fix Checking）+ 保持不变套件通过（Preservation）。

所有任务回链到具体缺陷（1.x）与 `design.md` 的 **Correctness Properties（Property 1–4）** 与 **Preservation（3.1–3.7）**。

**Correctness Properties 对照（design.md 单一事实来源）**：
- **Property 1 — Init-Order Safety（类别 A）**：validates 2.1, 2.2, 2.3
- **Property 2 — Data-Race Freedom（类别 B，TSan）**：validates 2.4, 2.5, 2.7
- **Property 3 — No Use-After-Free（类别 C，ASan）**：validates 2.6, 2.8, 2.9, 2.10, 2.11
- **Property 4 — Preservation（¬C(X) 行为不变）**：validates 3.1–3.7

**构建/验证工具（复用项目既有构建系统）**：CMake 3.20+ + Conan 2.x；`scripts/backend/build.bat -debug`（Windows）/ `bash scripts/backend/build.sh --debug`（Linux）。测试目标 `OAuth2Test_test`（`ctest` 注册名 `OAuth2Tests`），测试源按既有布局 `OAuth2Server/test/{unit,integration,e2e,security,performance}/` 由 `GLOB_RECURSE` 自动收录。Sanitizer 分别以 `-fsanitize=thread`（TSan）与 `-fsanitize=address -fno-omit-frame-pointer -g`（ASan）构建，二者不可同时启用。

## Task Dependency Graph

```
                          [0] Sanitizer 构建与测试脚手架（TSan/ASan）
                                           │
        ┌──────────────────────────────────┼───────────────────────────────────┐
        ▼                                   ▼                                    ▼
 [1] Property 1                       [2] Property 2                       [3] Property 3
   类别 A 复现（快照/链接序）            类别 B 复现（TSan 竞争）               类别 C 复现（ASan UAF）
   1.1 / 1.2 / 1.3                      1.4 / 1.5（1.7 无可复现竞争）          1.6→1.8 / 1.9 / 1.10 / 1.11
        │                                   │                                    │
        └───────────────────────────────────┴───────────────────────────────────┘
                                           │
                                  [4] Property 4 基线捕获（¬C(X) 保持不变）
                                           │
   ┌───────────────────────────┬───────────────────────────┬───────────────────────────┐
   ▼                           ▼                           ▼                             │
类别 A 修复 [6]              类别 B 修复 [7]              类别 C 修复 [8]                   │
 6.1 修复 1.1 (SIOF)         7.1 修复 1.4 (call_once)     8.2 修复 1.9（服务 ESFT，★不依赖 1.3）
 6.2 修复 1.2 (Meyers)       7.2 修复 1.5 (JwkManager)    8.3 修复 1.10（清理 weak★不依赖 1.3）
 6.3 修复 1.3 ⚠️大改/高风险   7.3 修复 1.7 (护栏)           8.1 修复 1.8（含 1.6）★★依赖 6.3
      │                                                   8.4 修复 1.11（getStorage→shared）★★依赖 6.3
      │  🔴 HARD BLOCKING PREREQUISITE                         │
      └──────────────────────────────────────────────────────►┘
                                           │
                                  [9] Checkpoint：全量 TSan/ASan 干净 + 全测试通过
```

执行波次（waves）—— 同一 wave 内任务可并行，wave 间顺序执行：

```json
{
  "waves": [
    {
      "wave": 1,
      "description": "测试脚手架：TSan/ASan 双构建与并发/关停测试基础设施",
      "tasks": ["0"]
    },
    {
      "wave": 2,
      "description": "在未修复代码(F)上复现缺陷并捕获基线（三类复现 + Preservation 基线可并行）",
      "tasks": ["1", "2", "3", "4"]
    },
    {
      "wave": 3,
      "description": "无 6.3 前置阻塞的修复：类别 A 的 SIOF(6.1/6.2)、类别 B(7.1/7.2/7.3)、类别 C 中已 make_shared 的服务(8.2/8.3)",
      "tasks": ["6.1", "6.2", "7.1", "7.2", "7.3", "8.2", "8.3"]
    },
    {
      "wave": 4,
      "description": "硬前置：所有权重构 storage_ → shared_ptr（高风险大改，须在测试护栏后单独落地）",
      "tasks": ["6.3"]
    },
    {
      "wave": 5,
      "description": "依赖 6.3 的类别 C 修复：1.8(含 1.6) 与 1.11（getStorage 重载 + 链路捕获 shared_ptr）",
      "tasks": ["8.1", "8.4"]
    },
    {
      "wave": 6,
      "description": "各类别修复验证（重跑同一批复现/基线测试）",
      "tasks": ["6.4", "7.4", "8.5"]
    },
    {
      "wave": 7,
      "description": "最终检查点：全量 TSan/ASan 干净 + 全测试通过",
      "tasks": ["9"]
    }
  ]
}
```

**关键依赖（务必遵守）**：

- 🔴 **6.3（修复 1.3：`storage_` → `shared_ptr`，由具体类型 `make_shared`）是 8.1（修复 1.8）与 8.4（修复 1.11）的硬性阻塞前置**。`CachedOAuth2Storage` 当前由 `unique_ptr<IOAuth2Storage>` 持有，未武装控制块，`shared_from_this()` 会抛 `std::bad_weak_ptr`。**必须先 6.3 再 8.1/8.4**。
- ★ **8.2（1.9）/ 8.3（1.10）不被 6.3 阻塞**：`TokenService`/`IdentityService`/`ClientService`/`OAuth2CleanupService` 已由 `make_shared` 持有，加上 `enable_shared_from_this` 基类后 `shared_from_this()`/`weak_from_this()` 即刻有效。
- ⚠️ **6.3 是本计划风险最高、改动面最大的任务**（触及 `IOAuth2Storage` 及所有服务/控制器的存储指针签名）。**必须先有 [1]–[4] 的复现/基线测试护栏，再落地 6.3**，并建议单独分支、分小步提交、逐步验证。

---

## Tasks

### 阶段 0：测试脚手架（复现的前置）

- [x] 0. 搭建 TSan/ASan Sanitizer 构建与并发/关停测试脚手架
  - 在 CMake 中新增可切换的 Sanitizer 选项（例如 `OAUTH2_SANITIZER=thread|address`），为 `OAuth2Test_test` 目标追加 `-fsanitize=thread` 或 `-fsanitize=address -fno-omit-frame-pointer -g`（编译与链接均加），仅在 GCC/Clang 下生效
  - 复用项目既有构建系统：`scripts/backend/build.bat -debug`（Windows）/ `bash scripts/backend/build.sh --debug`（Linux）+ Conan 2.x + CMake；Sanitizer 仅用于 Debug 构建
  - 新增测试源放在既有布局下，依赖 `test/CMakeLists.txt` 中 `GLOB_RECURSE` 自动收录：
    - 并发/关停竞态集成测试 → `OAuth2Server/test/integration/concurrency/`（被 `INTEGRATION_TESTS` 收录）
    - 纯函数/初始化单元测试 → `OAuth2Server/test/unit/`（被 `UNIT_TESTS` 收录）
    - 关停/UAF 安全相关 → 可置于 `OAuth2Server/test/security/`（被 `SECURITY_TESTS` 收录）
  - 提供运行入口：构建后运行 `OAuth2Test_test`（`ctest` 注册名 `OAuth2Tests`），并能分别在 TSan / ASan 两套构建下执行
  - **注意**：TSan 与 ASan 不可同时启用，需分别构建两个目标/两次构建
  - 不在此任务断言任何缺陷，仅确保两套 sanitizer 构建可编译、可运行既有测试套件
  - _Requirements: 2.4, 2.5, 2.6, 2.8, 2.9, 2.10, 2.11_

---

### 阶段 1：缺陷复现（在未修复代码 F 上，先于任何修复）

- [x] 1. 编写类别 A（初始化顺序/SIOF）复现测试
  - **Property 1: Bug Condition** - 初始化顺序安全（Init-Order Safety，类别 A：1.1 / 1.2 / 1.3）
  - **重要**：本测试必须在**未修复代码**上编写并运行，用于确认/否证 SIOF 与裸存储指针根因
  - **目标**：surface counterexamples —— 暴露"文档/规则在某些链接或初始化顺序下不完整"，以及"`storage_` 裸指针生命周期约定脆弱"
  - **Scoped 复现**：SIOF 依平台/链接顺序显现不稳定，采用**确定性可验证替代**：
    - 1.1：对 `/.well-known/openid-configuration`、JWKS 及各端点的 OpenApi 文档做**快照**，断言端点集合/参数/响应示例完整（建立基线；若链接序变化导致端点缺失即为反例）
    - 1.2：断言 `RequestValidationFilter::getValidationRules(path)` 对各 path 返回完整规则集（字段/min/max/pattern/enabled）
    - 1.3：编写"`shutdown()` 时 `storage_.reset()` 先于服务/在途回调"的时序断言（与类别 C 的关停竞态共用脚手架），记录裸指针失效路径
  - 在 F 上运行：快照建立基线；SIOF 反例可能依链接顺序间歇显现（记录观察）
  - **预期结果**：建立可对比基线；记录 1.1/1.2 在异常链接序下文档/规则不完整、1.3 裸指针时序脆弱的反例
  - 文档化发现的反例（端点缺失/规则空 map/裸指针在 reset 后被回调访问）
  - _Requirements: 2.1, 2.2, 2.3_

- [x] 2. 编写类别 B（线程安全/数据竞争）复现测试（TSan）
  - **Property 2: Bug Condition** - 无数据竞争（Data-Race Freedom，类别 B：1.4 / 1.5；1.7 见说明）
  - **重要**：本测试必须在**未修复代码**上以 **ThreadSanitizer** 构建运行
  - **目标**：surface counterexamples —— TSan 对无同步共享可变状态报出 read/write data race
  - **Scoped PBT 方法**：用属性测试生成随机并发交错时序：
    - 1.4：多个 IO 线程并发首次进入 `AuthorizationFilter::doFilter()` → `loadConfig()`，对 `rules_`/`publicPaths_`/`initialized_` 的 check-then-act（来自 design 1.4）—— 预期 TSan 报 data race（`push_back` 同一 vector）
    - 1.5：请求线程并发调用 `JwkManager::signJwt()/getJwks()` 读 `rsaKey_/initialized_/kid_`，与 `init()` 写交错（来自 design 1.5）—— 预期 TSan 报读写竞争
  - **🔴 明确不做**：**不**把 `CacheMap` 跨 loop 访问列为 B 类竞争用例 —— `drogon::CacheMap` 自带互斥锁，跨线程访问**不会**触发 race；强行预期 race 会按"无法复现即否证根因"规则**自我否证**。其真实问题（成员随宿主析构的 UAF）由任务 3（类别 C / ASan）覆盖
  - **1.7（潜在缺陷）**：`OAuth2Metrics` 当前仅 `LOG_INFO`、无共享计数器，**无可复现的 race**；本任务**不为 1.7 编写复现用例**，仅在修复任务 7.3 中作为"引入共享计数须原子"的护栏处理
  - 在 F 上运行（TSan）
  - **预期结果**：1.4 / 1.5 出现 TSan data-race 反例（will fail on unfixed code）
  - 文档化 TSan 报告的 read/write 竞争栈
  - _Requirements: 2.4, 2.5, 2.7_

- [x] 3. 编写类别 C（异步回调生命周期/UAF）复现测试（ASan）
  - **Property 3: Bug Condition** - 无 use-after-free（No Use-After-Free，类别 C：1.6→1.8 / 1.9 / 1.10 / 1.11）
  - **重要**：本测试必须在**未修复代码**上以 **AddressSanitizer** 构建运行
  - **目标**：surface counterexamples —— 构造"对象销毁先于在途异步回调"的关停竞态，ASan 报 heap-use-after-free
  - **Scoped PBT 方法**：生成随机"对象销毁 / 回调到达"的交错时序：
    - **1.8（含原 1.6 的 `tokenCache_/clientCache_` 成员）**：发起 `CachedOAuth2Storage` 的 `getAccessToken/saveAccessToken/revokeAccessToken` 异步操作后立即 `storage_.reset()`，让在途回调经悬垂 `this` 访问 `tokenCache_/clientCache_/redisClient_`（来自 design 1.8）
    - **1.8 直连扩展**：在 **redis 模式**对 `RedisOAuth2Storage::revokeAccessToken/atomicRevokeRefreshToken`、在 **postgres 无缓存回退路径**对 `PostgresOAuth2Storage::revokeAccessToken` 做同样的关停竞态（无外层 `CachedOAuth2Storage` 保护）
    - **1.9**：销毁 `TokenService`/`IdentityService` 后让 `exchangeCodeForToken`/`refreshAccessToken`/`ensureSubjectMapping`/`handleFirstTimeLogin` 链回调到达（来自 design 1.9）
    - **1.10**：`OAuth2CleanupService::runCleanup()` 在 Redis 命令派发与回复之间销毁服务，回调经悬垂 `this` 调 `storage_->deleteExpiredData()`（来自 design 1.10）
    - **1.11**：`OAuth2StandardController` 经 `getStorage()->...` 跨异步持裸存储指针（如 `client_credentials`、`userinfo` 链路），构造存储先于回调释放的时序（来自 design 1.11）
  - 在 F 上运行（ASan）
  - **预期结果**：上述场景出现 ASan heap-use-after-free 反例（will fail on unfixed code）
  - **注意**：`CacheMap` 的反例应出现在 **ASan（成员随宿主析构的 UAF）**，而**非 TSan**
  - 文档化 ASan 报告的 use-after-free 栈
  - _Requirements: 2.6, 2.8, 2.9, 2.10, 2.11_

---

### 阶段 2：保持不变基线（在未修复代码 F 上）

- [x] 4. 编写正常路径保持不变（Preservation）属性测试并捕获基线
  - **Property 4: Preservation** - 正常路径行为不变（Behavioral Equivalence on ¬C(X)）
  - **重要**：遵循 observation-first 方法 —— 先在**未修复代码（F）**上观测并记录基线，再断言 F' 与基线逐位一致
  - **观测对象（¬C(X)：顺序初始化 / 运行期只读 / 对象存活期内回调 / 已正确同步访问）**：
    - 3.1：正常顺序下 OpenApi 文档内容与 `getValidationRules(path)` 规则集（端点/参数/规则）
    - 3.2：授权码、令牌签发/刷新/校验/内省/吊销在正常时序下的响应/错误码/JSON 结构
    - 3.3：相同密钥/`kid`/RS256 下签发的 id_token / JWKS
    - 3.4：缓存命中（L1/L2）/未命中回源 DB 的语义（写穿、回源、失效、TTL），**包括 L1 命中的同步回调控制流**
    - 3.5：对象存活期内异步回调正常完成的业务副作用（入库、缓存写入、审计日志、清理任务）
    - 3.6：`OAuth2CleanupService` 在分布式锁可用/不可用两场景的"获锁才清理 / 无锁单实例清理"语义
    - 3.7：`AuthorizationFilter` 初始化完成后对已认证/未认证请求的 RBAC 与 public path 判定（放行/401/403）
  - 用 PBT 生成随机正常输入（随机 client/scope/token/grant 组合、随机请求路径、缓存命中/未命中序列），记录 F 的可观测输出为基线
  - **PBT 一致性放宽**：look-aside 并发未命中导致的 N 次 DB 回源（cache stampede）属允许的非线性一致行为 —— 断言比较**最终收敛值**，不要求严格线性一致
  - 在 F 上运行
  - **预期结果**：测试在未修复代码上**通过**（确立必须保持的基线行为）
  - _Requirements: 3.1, 3.2, 3.3, 3.4, 3.5, 3.6, 3.7_

---

### 阶段 3：修复实现

### 6. 类别 A 修复 —— 初始化顺序 / SIOF（1.1、1.2、1.3）

- [x] 6. 修复类别 A：消除初始化顺序依赖与隐式裸指针所有权

  - [x] 6.1 修复 1.1：OpenApi 文档注册的 SIOF
    - 删除 `OAuth2StandardController.cc` 中"靠构造副作用注册"的文件作用域全局对象 `OAuth2StandardControllerDocs docs_;`
    - 改为在 `OAuth2Plugin::initAndStart()`（或专门的 `registerApiDocs()`）中**显式调用** `OAuth2StandardController::initApiDocs()`（首选方案 1）
    - 此时 `OpenApiGenerator` 的函数内静态以 Meyers Singleton 语义安全初始化，不再依赖跨 TU 全局构造次序
    - _Bug_Condition: isBugCondition_A(X) where dependsOnAnotherGlobalCtorOrder(X)（1.1）_
    - _Expected_Behavior: 任意链接/初始化顺序下文档完整且正确注册（design Property 1）_
    - _Preservation: 注册端点集合/参数/响应示例与现状逐字一致（3.1）_
    - _Requirements: 2.1, 3.1_

  - [x] 6.2 修复 1.2：`OAUTH2_VALIDATION_RULES` 改为函数内静态访问器（Meyers Singleton）
    - 把文件作用域非平凡全局 `std::map` 改为函数内静态访问器 `rules()`：`static const std::map<...> kRules = buildRules();`
    - `buildRules()` 返回完整 map，**合并构造与一次性填充**，去掉独立的 `call_once`/`initFlag`（C++11 起函数局部静态首次初始化线程安全且仅一次）
    - `getValidationRules()` 改为读 `rules()`，逻辑不变
    - **澄清**：1.2 纯属 SIOF（类别 A），现有 `getValidationRules` 已线程安全；本修复只针对全局构造次序
    - _Bug_Condition: isBugCondition_A(X) where nonTrivialFileScopeGlobalWithRuntimeFill(X)（1.2）_
    - _Expected_Behavior: 任意全局初始化顺序下读到完整规则集（design Property 1）_
    - _Preservation: 同一 path 返回的规则（字段/min/max/pattern/enabled）与现状一致；校验放行/拒绝不变（3.1, 3.7）_
    - _Requirements: 2.2, 3.1, 3.7_

  - [x] 6.3 修复 1.3：`storage_` 改为共享所有权（`shared_ptr`）—— ⚠️ **高风险 / 大改动 / 8.1 与 8.4 的硬性前置**
    - **⚠️ 风险标注**：本任务触及 `IOAuth2Storage` 接口签名与所有使用点（各服务构造函数、控制器 `getStorage()`），属本计划改动面最大、回归风险最高的任务。**务必先确保阶段 1–2 的复现/基线测试护栏到位（[1]–[4]），再在独立分支分小步落地、逐步验证**
    - 把 `OAuth2Plugin` 的 `std::unique_ptr<IOAuth2Storage> storage_;` 改为 `std::shared_ptr<IOAuth2Storage> storage_;`
    - **🔴 关键接线（控制块必须绑定具体派生类型）**：`storage_` 必须经 `std::make_shared<CachedOAuth2Storage>(...)`（或直连模式下 `make_shared<RedisOAuth2Storage>/<PostgresOAuth2Storage>`）由**具体类型**创建，再隐式转换为 `shared_ptr<IOAuth2Storage>`
    - **🔴 严禁**：把已有的 `unique_ptr<IOAuth2Storage>` `std::move` 进 `shared_ptr<IOAuth2Storage>` —— 那样控制块绑定到**基类**，`enable_shared_from_this<具体类型>` 的 `weak_this` 不会被武装，后续 8.1 的 `shared_from_this()` 仍抛 `bad_weak_ptr`
    - 各服务（`TokenService`/`ClientService`/`IdentityService`/`OAuth2CleanupService`）的存储持有改为 `std::shared_ptr<IOAuth2Storage>`（或 `weak_ptr` + 用时 `lock()`），不再持 `storage_.get()` 裸指针
    - 明确销毁顺序：`shutdown()` 先 `cleanupService_->stop()`（停定时器），再释放服务，最后释放存储；配合 8.x 的 `shared_from_this`/`weak_from_this`，使在途回调安全完成或安全跳过
    - _Bug_Condition: isBugCondition_A(X) where usesRawStoragePointerAcrossLifetime(X)（1.3）_
    - _Expected_Behavior: 存储与各服务生命周期关系显式且被保证（共享所有权）（design Property 1）_
    - _Preservation: 核心流程结果不变、缓存语义不变、正常回调副作用不变（3.2, 3.4, 3.5）_
    - _Requirements: 2.3, 3.2, 3.4, 3.5_

  - [x] 6.4 验证类别 A 修复
    - **Property 1: Expected Behavior** - 初始化顺序安全（Init-Order Safety）
    - **重要**：重跑任务 1 的**同一**测试 —— 不要新写测试
    - 重跑任务 1：断言任意链接/初始化顺序下 OpenApi 文档与校验规则集完整一致；`storage_` 生命周期由 `shared_ptr` 保证
    - **预期结果**：测试 PASS（确认 SIOF 与裸指针时序问题消除）
    - 重跑任务 4（Preservation）：断言文档/规则快照与基线逐位一致
    - _Requirements: 2.1, 2.2, 2.3（design Property 1）_

### 7. 类别 B 修复 —— 线程安全（1.4、1.5、1.7）

- [x] 7. 修复类别 B：为共享可变状态建立同步与只读契约

  - [x] 7.1 修复 1.4：`AuthorizationFilter::loadConfig()` 的并发 check-then-act
    - 在头文件新增**每实例非静态成员** `std::once_flag initFlag_;`（**不要**用函数局部 `static std::once_flag`——它跨实例共享，会导致"只有第一个实例被填充、其余空规则"的静默缺陷）
    - `loadConfig()` 改为 `std::call_once(initFlag_, [this]{ ... })` 保证每实例只加载一次
    - **异常安全（强保证）**：在**局部 vector** 中构建完整 `localRules`/`localPublic`（`std::regex(pattern)` 可能抛 `std::regex_error`、`push_back` 可能抛），全部成功后再 `rules_.swap(localRules); publicPaths_.swap(localPublic);` 整体提交，避免部分填充/重复追加
    - **🔴 删除非原子快路径**：**务必删除** `call_once` 之前的 `if (initialized_) return;` 非原子快路径（它仍与初始化体内的写竞争）；`call_once` 自带高效快路径。若确需保留 `initialized_` 标志，须改为 `std::atomic<bool>`
    - _Bug_Condition: isBugCondition_B(X) where concurrentCheckThenActWrite(X, S)（1.4）_
    - _Expected_Behavior: `rules_`/`publicPaths_` 恰好初始化一次，并发读无 data race（TSan 干净）（design Property 2）_
    - _Preservation: 加载后 `rules_`/`publicPaths_` 内容与现状一致；`checkAccess` 放行/401/403 不变（3.7）_
    - _Requirements: 2.4, 3.7_

  - [x] 7.2 修复 1.5：`JwkManager` 强制 init-once-then-read-only 契约
    - 把 `init()` 设计为"仅可调用一次"（内部 `initialized_` 守卫，重复调用记录错误并 no-op）；`signJwt()/getJwks()` 保持 `const` 只读
    - **目标态（推荐方案 2）**：`OAuth2Plugin` 持有 `std::shared_ptr<const JwkManager>` —— 先本地构造并 `init()`，完成后以 `shared_ptr<const JwkManager>` 发布给 `tokenService_` 等（`const` 指针从类型层面禁止运行期变更）；`setJwkManager()` 等签名相应改为传 `shared_ptr<const JwkManager>`
    - 文档化 happens-before：只要 `init()` 在"开始接受请求并向事件循环投递任务"之前完成，运行期只读即无竞争、无需逐次加锁（`queueInLoop`/事件循环投递构成 release→acquire 配对）
    - **OpenSSL 并发假设（务必文档化）**：`signJwt()` 中 `EVP_MD_CTX` 已是每次调用 `new/free`（并发签名安全关键）；唯一并发共享的可变对象是 `EVP_PKEY`。**本设计明确假设 OpenSSL >= 1.1.0 且 threads-enabled**（原子引用计数 + 自动内部锁）；若项目可能链接 1.0.2，须补 legacy `CRYPTO_set_locking_callback`/`CRYPTO_THREADID` 回调或升级 OpenSSL
    - **弃用迁移提示（非本次安全必需）**：`getPublicKeyComponents()` 的 `EVP_PKEY_get1_RSA` 在 OpenSSL 3.0 已弃用，建议迁移到 `EVP_PKEY_get_bn_param(..., OSSL_PKEY_PARAM_RSA_N/E, ...)`
    - _Bug_Condition: isBugCondition_B(X) where concurrentReadWhileMutatingWithoutSync(X, S)（1.5）_
    - _Expected_Behavior: 运行期读到完全初始化的一致密钥状态，无读写竞争（TSan 干净）（design Property 2）_
    - _Preservation: 相同密钥/`kid`/RS256，签发一致的 id_token/JWKS（3.3）_
    - _Requirements: 2.5, 3.3_

  - [x] 7.3 修复 1.7：`OAuth2Metrics` 共享计数护栏（潜在缺陷，预防性）
    - **说明**：当前仅 `LOG_INFO`、无共享计数器，**无真实竞争可复现**；本任务为护栏而非修复既有 race
    - 文档化契约：把头部注释的"假设松一致性"替换为明确约束——"计数必须原子；不得引入非原子共享可变状态"
    - 规定：未来引入任何进程级共享计数**必须**使用 `std::atomic<...>`，或直接接入成熟指标库（`config.json` 已含 `PromExporter` 的 `Counter`/`Gauge`），避免自造易错轮子
    - 若沿用"日志即指标"则保持现状（`LOG_INFO` 本身线程安全），不引入共享可变计数
    - _Bug_Condition: isBugCondition_B(X) where sharedCounterWithoutAtomicity(X, S)（1.7，潜在）_
    - _Expected_Behavior: 任何共享计数以原子/线程安全手段维护，无 race、无计数丢失（design Property 2）_
    - _Preservation: 可观测性不改变业务响应（3.2 间接）_
    - _Requirements: 2.7, 3.2_

  - [x] 7.4 验证类别 B 修复
    - **Property 2: Data-Race Freedom** - 线程安全 / 无数据竞争
    - **重要**：重跑任务 2 的**同一** TSan 测试 —— 不要新写测试
    - 重跑任务 2（TSan）：断言 1.4 / 1.5 不再出现 data race（TSan 无 warning）
    - **预期结果**：测试 PASS（确认无数据竞争）
    - 重跑任务 4（Preservation）：断言 RBAC/规则判定、JWKS/id_token 与基线一致
    - _Requirements: 2.4, 2.5, 2.7（design Property 2）_

### 8. 类别 C 修复 —— 异步回调生命周期（1.6→1.8、1.9、1.10、1.11）

> 统一模式：带异步回调的对象继承 `std::enable_shared_from_this<T>`，回调捕获 `self`（`shared_ptr`，副作用必须完成）或 `weak_ptr`（用时 `lock()`，可丢弃任务）替换裸 `this`/裸指针。

- [x] 8. 修复类别 C：统一异步回调生命周期模式

  - [x] 8.2 修复 1.9：`TokenService`/`IdentityService` 异步链的悬垂 `this`（★ **不依赖 6.3**）
    - **说明**：这些服务**已由 `make_shared` 持有**，加上 `enable_shared_from_this` 基类后 `shared_from_this()` 即刻有效，**无 1.8 那种 `bad_weak_ptr` 接线陷阱**，可先于 8.1 实施
    - 让 `TokenService`、`IdentityService`（及同源的 `ClientService`）继承 `std::enable_shared_from_this<...>`
    - 每条异步链最外层捕获 `auto self = shared_from_this();`，内层 lambda 透传 `self`（而非 `this`），成员经 `self->` 访问（`storage_/accessTokenTtl_/jwkManager_`）；深层嵌套链逐层透传同一 `self`
    - 与 7.2 协同：`jwkManager_` 经 `shared_ptr<const JwkManager>` 由 `self->jwkManager_` 安全读取；与 6.3 协同：`storage_` 改 `shared_ptr` 后回调持 `self` 即间接保活
    - **排除项（¬C(X)，不改造）**：`OAuth2Plugin::validatePkceCodeVerifier` 经 `oauth2::TokenService(nullptr)` 临时对象调用的**纯函数**用法无异步、无成员依赖，保持不变
    - _Bug_Condition: isBugCondition_C(X)：捕获裸 this 且服务可能先于回调销毁（1.9）_
    - _Expected_Behavior: 回调安全持有对象完成或安全跳过，无 UAF（ASan 干净）（design Property 3）_
    - _Preservation: 核心令牌流程响应/错误码/JSON 不变；正常回调副作用（入库/审计）不变（3.2, 3.5）_
    - _Requirements: 2.9, 3.2, 3.5_

  - [x] 8.3 修复 1.10：`OAuth2CleanupService::runCleanup()` 对齐 `weak_from_this()`（★ **不依赖 6.3**）
    - **说明**：该类**已继承** `enable_shared_from_this` 且 `start()/runEvery` 已正确用 `weak_from_this()`；本修复只是把 `runCleanup()` 对齐为同一模式（实现不一致修复）
    - `runCleanup()` 取 `auto weakSelf = weak_from_this();`，**成功回调与异常回调均捕获 `weakSelf`**，入口 `auto self = weakSelf.lock(); if (!self || !self->running_) return;`，再经 `self->storage_->deleteExpiredData()`
    - **推荐 `weak_ptr` 而非 `shared_ptr`**：清理任务周期性、可丢弃 —— 关停时跳过本次清理是正确行为（与 `stop()` 的 `running_=false` 守卫意图一致），不应延长寿命
    - _Bug_Condition: isBugCondition_C(X)：runCleanup 捕获裸 this 且服务可能在派发与回复间销毁（1.10）_
    - _Expected_Behavior: 对象已销毁时安全跳过，绝不经悬垂 this 访问 `storage_`（ASan 干净）（design Property 3）_
    - _Preservation: 分布式锁可用/不可用两场景的"获锁才清理 / 无锁单实例清理"语义不变（3.6）_
    - _Requirements: 2.10, 3.6_

  - [x] 8.1 修复 1.8（含原 1.6）：`CachedOAuth2Storage` 及直连存储异步回调中的悬垂 `this`（🔴 **依赖 6.3**）
    - **🔴 前置硬约束**：必须先完成 6.3（`storage_` → `shared_ptr` 且由具体类型 `make_shared`）。否则 `CachedOAuth2Storage::shared_from_this()` 抛 `std::bad_weak_ptr`
    - 让 `CachedOAuth2Storage`、`RedisOAuth2Storage`、`PostgresOAuth2Storage` 各自继承 `std::enable_shared_from_this<T>`（理想覆盖整条 `IOAuth2Storage` 装饰/实现链）
    - 所有异步 lambda 由 `[this, ...]` 改为捕获 `auto self = shared_from_this();`（`[self, ...]`，成员经 `self->`），保证回调执行期间成员（含 `tokenCache_/clientCache_`、`redisClient_`、DB 句柄）不被析构
    - **覆盖直连路径（无外层 `CachedOAuth2Storage` 保护）**：
      - **redis 模式**：`RedisOAuth2Storage::revokeAccessToken`、`atomicRevokeRefreshToken` 直连，需各自修复
      - **postgres 无缓存回退**：`PostgresOAuth2Storage::revokeAccessToken` 直连，需修复
      - 直连实现同样须由 `make_shared` 创建并以 `shared_ptr` 持有，其 `shared_from_this()` 才有效
    - **原 1.6 协同（修订）**：`drogon::CacheMap` 自带互斥锁、线程安全，**不需要** `queueInLoop` 编排；捕获 `self` 即同时保护 `tokenCache_/clientCache_` 成员生命周期。**🔴 严禁引入会改变 L1 命中同步控制流的 loop 编排**（否则回归 3.4/3.5）
    - **🔴 嵌套所有权决策点（NESTED ownership，必须二选一并全链路一致）**：`CachedOAuth2Storage::impl_` 是 `unique_ptr<IOAuth2Storage>` 独占持有内层 Postgres/Redis；被包裹时内层**自身的** `shared_from_this()` 会抛 `bad_weak_ptr`。一个"三个类都无条件调用各自 `shared_from_this()`"的朴素实现会在缓存/包裹路径崩溃。**择一**：
      - **方案 A（首选）**：被包裹路径**不依赖内层自身 ESFT** —— 由**外层 `CachedOAuth2Storage` 捕获自身 `self`**，因外层独占持有 `impl_`，`self` 存活即传递性保活 `impl_`；内层仅在**直连角色**下使用自己的 `shared_from_this()`
      - **方案 B**：把 `CachedOAuth2Storage::impl_` 改为 `std::shared_ptr<IOAuth2Storage>` 并经 `make_shared<具体类型>` 创建 —— 内层 `shared_from_this()` 在被包裹路径下也有效，内层回调统一自捕获 `self`
    - **关停交互（务必处理）**：捕获 `self` 会把 `~CachedOAuth2Storage`（及底层存储）析构延后到最后一个在途回调完成，且析构发生在 redis/DB 的 loop 线程。需保证延后析构时对应 loop 仍在运行 —— 在 `OAuth2Plugin::shutdown()` 中**先排空（drain）/等待在途操作完成，再 `storage_.reset()`**，或确保 `reset()` 与各客户端 loop 停止顺序使延后析构落在 loop 存活窗口内（对照 Drogon 关停序列在实现阶段确认）
    - **版本核对**：`CacheMap` 线程安全结论对照 upstream master，须对照项目锁定的 Drogon 版本复核
    - _Bug_Condition: isBugCondition_C(X)：捕获裸 this 且对象可能被 `storage_.reset()` 先于回调析构（1.8，含 1.6 的 CacheMap 成员）_
    - _Expected_Behavior: 回调安全持有对象完成，绝不经悬垂 this 访问 `tokenCache_/clientCache_/redisClient_` 或直连存储成员（ASan 干净）（design Property 3）_
    - _Preservation: 对象存活期内缓存命中/未命中/失效/TTL 行为及 **L1 命中同步回调时序** 与现状逐位一致（3.4, 3.5）_
    - _Requirements: 2.6, 2.8, 3.4, 3.5_

  - [x] 8.4 修复 1.11：`OAuth2StandardController` 长链异步中的裸存储指针（🔴 **依赖 6.3，但 6.3 不充分**）
    - **🔴 1.3 单独不足以修复本条**：把 `storage_` 改 `shared_ptr`（6.3）**不会**自动让控制器持有的存储指针变安全。必须**同时满足两点**：
      - (1) **`getStorage()` 的两个重载都返回 `shared_ptr<IOAuth2Storage>`**（而非裸指针/引用）
      - (2) 控制器异步链**捕获该 `shared_ptr` 并沿链透传**（而非取 `.get()` 裸指针），覆盖 `client_credentials` 授权、`userinfo` 等链路
    - 文档化框架保证：在控制器头部注释"Drogon 控制器为进程级单例，生命周期覆盖整个运行期；异步回调期间 `this` 始终有效"，把隐式约定转为显式契约
    - 统一捕获风格：保持"捕获所需局部 + `plugin` 指针"的风格，避免在回调中捕获裸 `this` 访问控制器可变成员（控制器应保持无可变共享状态）
    - _Bug_Condition: isBugCondition_C(X)：控制器经 `getStorage()->...` 跨异步持裸存储指针（1.11）_
    - _Expected_Behavior: 存储在异步期间由捕获的 `shared_ptr` 保活，裸 this/裸存储指针不再成为隐式风险面（ASan 干净）（design Property 3）_
    - _Preservation: 控制器端点响应/错误码/JSON 不变（3.2）_
    - _Requirements: 2.11, 3.2_

  - [x] 8.5 验证类别 C 修复
    - **Property 3: No Use-After-Free** - 异步回调生命周期安全
    - **重要**：重跑任务 3 的**同一** ASan 测试 —— 不要新写测试
    - 重跑任务 3（ASan）：断言 1.8（含 1.6）/ 1.9 / 1.10 / 1.11 在关停竞态下不再出现 heap-use-after-free
    - 覆盖 redis 模式与 postgres 无缓存路径下直连存储的关停竞态
    - **预期结果**：测试 PASS（确认无 UAF；回调安全完成或安全跳过）
    - 重跑任务 4（Preservation）：断言缓存语义（含 L1 命中同步控制流）、清理锁语义、核心流程与基线一致
    - _Requirements: 2.6, 2.8, 2.9, 2.10, 2.11（design Property 3）_

---

### 阶段 4：最终检查点

- [x] 9. Checkpoint —— 全量 sanitizer 干净 + 全测试通过
  - 在 **TSan** 构建下跑通端到端 OAuth2 流程（authorize → token → refresh → introspect → revoke）+ 类别 B 复现套件，确认 TSan 无 warning（Property 2）
  - 在 **ASan** 构建下跑通端到端流程 + 关停竞态集成测试（高并发请求中触发 `shutdown()`，覆盖 redis 模式与 postgres 无缓存路径），确认 ASan 无 heap-use-after-free（Property 3）
  - 跑通保持不变套件（任务 4）：文档/规则快照、核心流程响应、JWKS/id_token、缓存语义、清理锁语义与基线逐位一致（Property 4）
  - 确认初始化顺序安全（Property 1）：任意链接序下文档/规则完整
  - 清理验证过程中产生的临时文件/构建产物
  - 如有疑问或测试失败，向用户求证后再继续
  - _Requirements: 2.1, 2.2, 2.3, 2.4, 2.5, 2.6, 2.7, 2.8, 2.9, 2.10, 2.11, 3.1, 3.2, 3.3, 3.4, 3.5, 3.6, 3.7_

---

## Notes

**关键排序与依赖（务必遵守）**：

- 🔴 **6.3（修复 1.3：`storage_` → `shared_ptr`，由具体类型 `make_shared`）是 8.1（修复 1.8）与 8.4（修复 1.11）的硬性阻塞前置（HARD BLOCKING PREREQUISITE）**。`CachedOAuth2Storage` 当前由 `unique_ptr<IOAuth2Storage>` 持有，控制块未武装，`shared_from_this()` 会抛 `std::bad_weak_ptr`。**必须先 6.3 再 8.1/8.4**。任务编号已刻意让 8.2/8.3 排在 8.1/8.4 之前，以反映"不依赖 6.3 的先做"。
- ★ **8.2（1.9）/ 8.3（1.10）不被 6.3 阻塞**：`TokenService`/`IdentityService`/`ClientService`/`OAuth2CleanupService` 已由 `make_shared` 持有，加上 `enable_shared_from_this` 基类后 `shared_from_this()`/`weak_from_this()` 即刻有效。
- ⚠️ **6.3 是本计划风险最高、改动面最大的任务**（触及 `IOAuth2Storage` 接口签名及所有服务/控制器的存储指针）。**先让 [0]–[4] 的复现/基线测试护栏到位，再在独立分支分小步落地 6.3、逐步验证**（landed behind tests first）。

**方法论提醒**：

- 任务 1/2/3 为**复现测试**，必须在**未修复代码（F）**上**失败**（SIOF 快照差异 / TSan data race / ASan UAF）——失败即确认根因存在；**不要在此阶段修复测试或代码**。
- 任务 4 为 **Preservation 基线**，必须在 F 上**通过**（observation-first：先记录基线，再断言 F' 与之逐位一致）。
- 验证子任务（6.4 / 7.4 / 8.5）**重跑同一批测试**，不新写测试：复现测试转为 PASS（Fix Checking），基线测试保持 PASS（Preservation）。

**1.6 折叠说明**：原 1.6（`CacheMap` 跨 loop 访问）经复核**不是数据竞争**（`drogon::CacheMap` 自带互斥锁），已重归类为**生命周期问题并并入 1.8（任务 8.1）**。因此：**不**为 `CacheMap` 写 TSan 竞争用例、**不**加 `queueInLoop` 编排任务；其真实问题（成员随宿主析构的 UAF）由 ASan 用例（任务 3 / 8.1）覆盖。

**1.8 嵌套所有权决策点**：`CachedOAuth2Storage::impl_`（`unique_ptr`）包裹的内层 Postgres/Redis 自身 `shared_from_this()` 在被包裹路径下无效。任务 8.1 中必须**二选一并全链路一致**：方案 A（外层 `self` 传递性保活内层，首选）或方案 B（`impl_` 改 `make_shared` 的 `shared_ptr`）。

**关停（shutdown）交互**：捕获 `self` 会把析构延后到最后一个在途回调（运行在 redis/DB loop 线程）。需在 `shutdown()` 中**先排空在途回调再 `storage_.reset()`**，或保证 `reset()` 与各客户端 loop 停止顺序使延后析构落在 loop 存活窗口内（任务 6.3 / 8.1，对照 Drogon 关停序列在实现阶段确认）。

**OpenSSL 假设**：1.5/7.2 明确假设 **OpenSSL >= 1.1.0 且 threads-enabled**（`EVP_PKEY` 并发签名安全）；若可能链接 1.0.2 须补 legacy 锁回调或升级。`EVP_PKEY_get1_RSA` 在 3.0 弃用，建议迁移到 `EVP_PKEY_get_bn_param`（增强项，非本次安全必需）。

**版本核对**：`drogon::CacheMap` 线程安全结论对照 upstream master，实现阶段须对照项目锁定的 Drogon 版本复核。
