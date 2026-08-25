# 并发与生命周期安全审计 Bugfix 设计文档

## Overview

本设计文档为 `bugfix.md` 中枚举的 11 条缺陷（1.1–1.11）提供**修复方案设计（remediation design）**。

> **范围约束（务必遵守）**：本文档是**分析 + 修复计划**，**不在本阶段修改任何源代码**。下文为每条缺陷给出"根因 → 建议修复方式 → 取舍（tradeoffs）→ 如何保持回归防护行为（3.x）→ 验证策略"的完整设计，但**不落地实现**。是否实施、按何种优先级实施，属于后续独立决策（由 `tasks.md` 驱动）。

缺陷沿用 `bugfix.md` 的三大类别：

- **类别 A —— 静态/全局初始化顺序与相互依赖**（SIOF 及跨翻译单元依赖）：缺陷 1.1、1.2、1.3。
- **类别 B —— 多线程线程安全**（共享可变状态的数据竞争）：缺陷 1.4、1.5、1.7。
- **类别 C —— 异步回调的对象生命周期安全**（回调捕获裸 `this`/裸指针导致 use-after-free）：缺陷 1.6、1.8、1.9、1.10、1.11。

> **重要修订（审计复核结论）**：原始草案把缺陷 1.6（`CachedOAuth2Storage` 的 `CacheMap` 跨 loop 访问）归入类别 B 数据竞争，**此前提经复核后确认错误**。`drogon::CacheMap` **本身是线程安全的**（内部 `std::mutex mtx_` 保护 `map_`、`bucketMutex_` 保护时间轮；所有公共操作 `insert/erase/find/findAndFetch/operator[]/modify` 均加锁，`findAndFetch` 在锁内返回**副本**，`modify()` 文档标注多线程安全）；构造时传入的 `EventLoop*` **仅用于驱动周期性过期清理定时器，并非访问亲和性约束**。因此跨 loop 调用 `CacheMap` 的读写**是安全的**，1.6 不是数据竞争。1.6 真正残留的关注点是**对象生命周期**——若 `CachedOAuth2Storage` 在回调途中被析构，其 `CacheMap` **成员**随之析构——这与 1.8 同源，故 **1.6 已重新归类到类别 C（生命周期），并并入 1.8 的处理**。上述 `CacheMap` 线程安全结论系对照 **upstream master** 源码确认，**实现阶段应再对照项目锁定的 Drogon 版本复核**。

整体修复策略可概括为四条主线：

1. **消除初始化顺序依赖**：把 SIOF 易发的文件作用域全局对象，改为函数内静态（Meyers Singleton）或显式在插件启动阶段初始化（类别 A）。
2. **建立明确的 happens-before 与一次性初始化**：对运行期只读的状态（`JwkManager`、过滤器规则）以 `call_once`/启动期完成初始化 + 文档化只读契约约束；对真正的共享可变计数使用原子量（类别 B）。
3. **统一异步回调生命周期模式**：让带异步回调的对象继承 `enable_shared_from_this`，在 lambda 中捕获 `weak_ptr`/`shared_ptr` 而非裸 `this`/裸指针（类别 C，含原 1.6 折叠进 1.8 的 `CacheMap` 成员生命周期）。**注意**：`CacheMap` 本身线程安全，无需把其访问 `queueInLoop` 编排回某个 loop；保护它的方式与保护其它成员一致——回调持有 `self`（`shared_ptr`）即可。
4. **理顺所有权**：`storage_` 与依赖它的各服务之间，由隐式裸指针时序约定改为显式共享所有权（`shared_ptr`）。

> 重要参考库说明：本仓库工作区**未内置（vendored）Drogon 源码**（`libs/drogon` 不存在）。因此下文"库对照"一节引用的是**业界公认的 Drogon 惯用法**与公开 API 语义；若后续将 Drogon 源码纳入工作区，应在实现阶段以实际源码再次核对（例如 `trantor::EventLoop::queueInLoop/runInLoop`、`drogon::CacheMap` 的 loop 绑定构造、`runEvery` 的 timer 语义、`DbClient`/`RedisClient` 回调线程模型）。

## Glossary

- **Bug_Condition (C)**：触发缺陷的输入/时序条件。参见 `bugfix.md` 中的 `isBugCondition_A/B/C`。
- **Property (P)**：修复后（F'）对 C(X) 输入应满足的期望行为（无 SIOF、无数据竞争、无 UAF，且业务结果与现状一致）。
- **Preservation（保持不变）**：对 ¬C(X)（不触发缺陷的正常路径）必须与现状（F）逐位一致的行为，即回归防护（`bugfix.md` 3.1–3.7）。
- **F / F'**：修复前的现状函数 / 修复后的函数。
- **SIOF**：Static Initialization Order Fiasco，跨翻译单元的全局对象构造次序未定义导致的缺陷。
- **EventLoop / loop**：Drogon（基于 trantor）的事件循环线程。Drogon 默认以多个 IO 线程运行，每个连接/客户端绑定到某个 loop。
- **loop affinity（loop 亲和性）**：某些 trantor 资源（如连接、`DbClient`/`RedisClient` 的回调）在其所属 loop 线程上执行。**注意**：`drogon::CacheMap` **不属于** loop 亲和结构——它内部自带互斥锁、可在任意线程安全访问（见下条 CacheMap）。
- **queueInLoop / runInLoop**：trantor `EventLoop` 提供的把可调用对象编排到目标 loop 线程执行的原语（`runInLoop` 在当前即为目标 loop 时同步执行，否则入队；`queueInLoop` 始终入队）。
- **enable_shared_from_this / weak_from_this**：C++ 标准库机制，使对象能从成员函数安全获取自身的 `shared_ptr`/`weak_ptr`，用于延长或检测异步回调期间的对象生命周期。
- **storage_**：`OAuth2Plugin` 的 `std::unique_ptr<IOAuth2Storage>` 成员；`TokenService`/`ClientService`/`IdentityService`/`OAuth2CleanupService` 当前持有其 `.get()` 裸指针。
- **CacheMap**：`drogon::CacheMap<K,V>`，构造时接收一个 `EventLoop*`。**该 `EventLoop*` 仅用于驱动其内部周期性过期清理定时器，不构成访问亲和约束。** `CacheMap` **本身是线程安全的**：内部以 `std::mutex mtx_` 保护底层 `map_`、以 `bucketMutex_` 保护时间轮，所有公共操作（`insert/erase/find/findAndFetch/operator[]/modify`）均在锁内执行，`findAndFetch` 在锁内返回**值副本**，`modify()` 文档标注多线程安全。因此可在任意线程（含 Redis/DB 回调线程）安全读写。（结论对照 upstream master，实现阶段须对照项目锁定版本复核。）
- **JwkManager**：`OAuth2Plugin/src/utils/JwkManager.cc` 中管理 RS256 签名密钥的类，运行期为 id_token/JWKS 提供签名与公钥。

## Bug Details

> 下列形式化沿用 `bugfix.md`。**F** 为现状、**F'** 为修复后。每个 `isBugCondition` 用伪代码表达"何种输入/时序触发缺陷"。

### 类别 A —— 初始化顺序与依赖（1.1、1.2、1.3）

缺陷在进程**静态初始化阶段**或**插件初始化/销毁阶段**触发：某全局对象/服务在其依赖项尚未就绪、或所依赖对象的构造次序未定义时被构造或使用。

**Formal Specification:**
```
FUNCTION isBugCondition_A(X)
  INPUT: X = 静态初始化序列 / 插件初始化—销毁序列
  OUTPUT: boolean

  RETURN dependsOnAnotherGlobalCtorOrder(X)        // 1.1 docs_ → OpenApiGenerator 函数内静态
      OR nonTrivialFileScopeGlobalWithRuntimeFill(X) // 1.2 OAUTH2_VALIDATION_RULES + call_once
      OR usesRawStoragePointerAcrossLifetime(X)     // 1.3 services 持有 storage_.get()
END FUNCTION
```

#### Examples（缺陷如何显现）

- **1.1**：`OAuth2StandardController.cc` 文件作用域的 `OAuth2StandardControllerDocs docs_;` 在其构造函数中调用 `initApiDocs()`，后者调用 `OpenApiGenerator::addEndpoint(...)`，写入位于另一个翻译单元的 `OpenApiGenerator` 函数内静态注册表。若 `docs_` 的构造早于 `OpenApiGenerator` 的相关静态初始化完成 → 期望：文档完整注册；实际：注册次序未定义，可能写入未完全初始化的注册表（端点丢失/错乱，依链接顺序而定）。
- **1.2**：`RequestValidationFilter::OAUTH2_VALIDATION_RULES`（`std::map`，文件作用域非平凡全局）构造次序相对其它全局未定义；其内容由运行期 `std::call_once(initFlag, initializeValidationRules)` 填充，"构造时机"与"填充时机"分离 → 若有其它全局在其构造期访问该 map，可能读到空 map。
- **1.3**：`OAuth2Plugin::initAndStart()` 按 `storage_ → tokenService_/clientService_/identityService_ → cleanupService_` 构造，后几者保存 `storage_.get()` 裸指针。析构虽因成员声明顺序（`storage_` 最先声明 → 最后析构）目前"恰好正确"，但 `shutdown()` 显式 `storage_.reset()` 会在服务仍存活、且可能仍有在途回调时先释放底层存储 → 期望：存储生命周期覆盖所有使用者；实际：依赖隐式时序，重排成员或在 `shutdown()` 后回调到达即破坏约束。

### 类别 B —— 线程安全（1.4、1.5、1.7）

缺陷在**多个 IO 线程并发访问无同步保护的共享可变状态**时触发。

> **修订说明**：原草案的 1.6（`CacheMap` 跨 loop 访问）已从类别 B 移除——`drogon::CacheMap` 内部自带互斥锁，跨线程读写安全，不构成数据竞争。1.6 现作为**生命周期问题**并入类别 C 的 1.8（见下文）。

**Formal Specification:**
```
FUNCTION isBugCondition_B(X)
  INPUT: X = (并发时序, 共享可变状态 S, 访问所在线程/loop)
  OUTPUT: boolean

  RETURN concurrentCheckThenActWrite(X, S)          // 1.4 AuthorizationFilter::loadConfig
      OR concurrentReadWhileMutatingWithoutSync(X,S)// 1.5 JwkManager 写 init / 读 signJwt
      OR sharedCounterWithoutAtomicity(X, S)        // 1.7 OAuth2Metrics 引入共享计数后的潜在竞争
END FUNCTION
```

#### Examples

- **1.4**：多个 IO 线程首次进入 `AuthorizationFilter::doFilter()` → `loadConfig()`，对 `initialized_` 做"读到 false 就写 `rules_`/`publicPaths_`"的 check-then-act，无锁 → 多线程同时通过检查并发 `push_back` 同一 `std::vector`，数据竞争（崩溃或规则部分加载）。
- **1.5**：`JwkManager::init()` 写 `rsaKey_`/`initialized_`/`kid_` 的同时，请求线程调用 `signJwt()`/`getJwks()` 读这些成员，无同步原语 → 读写竞争。当前正确性仅靠"启动期初始化、运行期只读"的未文档化假设，且 `setJwkManager()`、`init()` 为非 const 变更入口未被约束。
- **1.7**：`OAuth2Metrics.cc` 当前仅 `LOG_INFO`，无共享计数器（暂无真实竞争），但注释自承"假设松一致性"。一旦引入真实共享计数器（如普通 `long` 自增）即成无同步竞争点（潜在缺陷）。

> **1.6（已移出类别 B）**：`CachedOAuth2Storage::tokenCache_/clientCache_` 在 Redis/DB 回调线程上被 `insert/erase/findAndFetch` —— **这是安全的**，因为 `drogon::CacheMap` 内部加锁。1.6 的真实问题是：若 `CachedOAuth2Storage` 在回调途中被析构，这些 `CacheMap` 成员随之析构而回调仍在访问（use-after-free）。该问题归入类别 C，与 1.8 合并处理。

### 类别 C —— 异步回调生命周期（1.6、1.8、1.9、1.10、1.11）

缺陷在**对象在其异步回调完成前被销毁**、而回调通过捕获的裸 `this`/裸指针访问已析构成员时触发（use-after-free）。**1.6 的本质同属此类**：`CacheMap` 作为 `CachedOAuth2Storage` 的**成员**，随宿主对象一同析构，回调经悬垂 `this` 触碰它即 UAF（与 1.8 同源，合并处理）。

**Formal Specification:**
```
FUNCTION isBugCondition_C(X)
  INPUT: X = (异步操作 op, 捕获方式 capture, 对象是否可能在回调完成前销毁)
  OUTPUT: boolean

  RETURN capturesRawThisOrRawPtr(capture)
     AND objectMayBeDestroyedBeforeCallback(op, X)
END FUNCTION
```

#### Examples

- **1.6（生命周期，非数据竞争）**：`CachedOAuth2Storage` 在 `getAccessToken/saveAccessToken/revokeAccessToken` 等异步回调中访问成员 `tokenCache_/clientCache_`。`CacheMap` 自身线程安全，跨 loop 读写无竞争；但若 `shutdown()` 的 `storage_.reset()` 在回调前析构 `CachedOAuth2Storage`，则 `tokenCache_/clientCache_` 这两个**成员**被析构，在途回调经悬垂 `this` 访问已析构的 `CacheMap` → UAF。与 1.8 完全同源，统一由 `enable_shared_from_this` 修复。
- **1.8**：`CachedOAuth2Storage::getAccessToken/saveAccessToken/revokeAccessToken` 把捕获裸 `this` 的 lambda 交给 `redisClient_->execCommandAsync(...)` 或 `impl_->...`；若 `OAuth2Plugin::shutdown()` 的 `storage_.reset()` 在回调前析构该对象 → 回调经悬垂 `this` 访问 `tokenCache_/clientCache_/redisClient_`（该类未继承 `enable_shared_from_this`）。**此外**，未经 `CachedOAuth2Storage` 包裹的底层存储也直接持有同类风险，必须一并覆盖：
  - **`RedisOAuth2Storage`**：`revokeAccessToken`、`atomicRevokeRefreshToken` 在 `execCommandAsync` 回调中捕获裸 `[this]`；在 **redis 模式**下该实现被直接使用（无外层 `CachedOAuth2Storage` 保护）。
  - **`PostgresOAuth2Storage`**：`revokeAccessToken` 在异步回调中捕获裸 `[this]`；在 **postgres 无缓存回退路径**下被直接使用（无外层 `CachedOAuth2Storage` 保护）。
- **1.9**：`TokenService`/`IdentityService` 的多层异步链（`exchangeCodeForToken`、`refreshAccessToken`、`ensureSubjectMapping`、`handleFirstTimeLogin` 等）捕获裸 `this` 透传到 `storage_->...`；服务对象先于回调销毁时，回调经悬垂 `this` 访问 `storage_/accessTokenTtl_/jwkManager_`（两类均未继承 `enable_shared_from_this`）。
- **1.10**：`OAuth2CleanupService::runCleanup()` 把捕获裸 `this` 的 lambda 交给 `redis->execCommandAsync(...)`；服务在命令派发与回复之间销毁时，回调经悬垂 `this` 调用 `storage_->deleteExpiredData()`。**与同文件 `start()` 中 `runEvery` 已正确使用 `weak_from_this()` 不一致**（类已继承 `enable_shared_from_this`，但 `runCleanup` 未沿用）。
- **1.11**：`OAuth2StandardController` 授权/令牌链逐层透传裸 `this` 到底层异步存储调用；控制器为进程级单例、实际崩溃风险较低，但裸 `this` 成为隐式风险面，缺乏显式生命周期保障。**更具体的风险**：控制器通过 `getStorage()->...` 跨异步持有**裸存储指针**（如 `client_credentials` 授权、`userinfo` 等链路），存储对象本身的生命周期不受控制器单例保护。**注意此点不会被 1.3 单独修复**——只有当 `getStorage()` 的**两个重载都返回 `shared_ptr`**、且控制器链路**捕获该 `shared_ptr`**（而非裸指针）时，存储生命周期才安全。

## Expected Behavior

本节聚焦**必须保持不变（Preservation）**的行为；修复后对 C(X) 输入的期望正确行为（P）在「## Correctness Properties」中统一定义，作为单一事实来源。

### Preservation Requirements（对应 bugfix.md 3.1–3.7）

**Unchanged Behaviors（必须逐位一致）：**
- **3.1**：单线程/正常启动顺序下，OpenApi 文档内容与请求校验规则集（端点、参数、规则）与现状一致。
- **3.2**：授权码、令牌签发/刷新/校验/内省/吊销等核心流程在正常时序下返回**完全相同**的成功响应、错误码与 JSON 结构。
- **3.3**：初始化完成后运行期对 `JwkManager` 只读访问，使用相同签名密钥、`kid` 与 RS256 算法，签发一致的 id_token / JWKS。
- **3.4**：缓存命中（L1/L2）/未命中回源 DB 的读写路径，维持相同的缓存语义（写穿、回源、失效、TTL 不变）。
- **3.5**：对象生命周期内（未销毁）异步回调正常完成时，业务回调与副作用（令牌入库、缓存写入、审计日志、清理任务）与现状一致。
- **3.6**：`OAuth2CleanupService` 在分布式锁可用/不可用两种场景下的"获锁才清理 / 无锁单实例清理"语义不变。
- **3.7**：`AuthorizationFilter` 初始化完成后对已认证/未认证请求的 RBAC 与 public path 判定（放行/401/403）不变。

**Scope（不受本次修复影响的输入）：**
所有"不触发"上述缺陷条件的正常路径——即 ¬C(X)：单实例顺序初始化、运行期只读访问、对象存活期内的异步回调、单线程或已正确同步的访问——其外部可观测行为必须与现状逐位一致。本次修复的目标是**仅改变并发/销毁竞态下的行为**（从 UB/UAF/数据竞争变为安全），而不改变任何正常路径的业务语义。

## Hypothesized Root Cause

按类别归纳根因（具体到每条缺陷的根因在「## Fix Implementation」逐条展开）：

1. **类别 A 根因——依赖跨翻译单元的全局构造次序**：
   - 1.1 用文件作用域全局对象（`docs_`）的构造副作用去写另一个 TU 的全局注册表，构造次序未定义。
   - 1.2 用文件作用域非平凡全局 `std::map`，且填充依赖运行期 `call_once`，构造与填充时机分离。
   - 1.3 用裸指针（`storage_.get()`）表达跨对象依赖，生命周期约束停留在隐式约定而非类型系统。

2. **类别 B 根因——共享可变状态缺乏同步**：
   - 1.4 check-then-act 无 `call_once`/锁。
   - 1.5 "启动期写、运行期读"未由代码强制（无 const-only 契约、无内存序保证）。
   - 1.7 设计注释默认"松一致性"，为将来引入非原子共享计数埋下隐患。
   - （**原 1.6 已移出**：`CacheMap` 自带互斥锁，跨 loop 访问安全，非数据竞争；其残留的生命周期问题归入类别 C。）

3. **类别 C 根因——异步回调捕获裸 `this`/裸指针，未与对象生命周期挂钩**：
   - 1.6/1.8 相关类（`CachedOAuth2Storage` 及未被其包裹的 `RedisOAuth2Storage`/`PostgresOAuth2Storage`）未继承 `enable_shared_from_this`，回调持有的裸 `this` 在对象析构后悬垂——访问 `CacheMap` 成员或 `redisClient_` 即 UAF。
   - 1.9 相关类未继承 `enable_shared_from_this`，无法在回调中安全延长/检测生命周期。
   - 1.10 类虽已继承 `enable_shared_from_this` 并在 `runEvery` 用了 `weak_from_this()`，但 `runCleanup` 的 Redis 回调遗漏了同样的保护（实现不一致）。
   - 1.11 控制器单例长生命周期掩盖了风险，但其跨异步持有的**裸存储指针**缺乏显式、统一的生命周期策略。

## Correctness Properties

> 本节是所有正确性属性的**单一事实来源**，供 PBT 追溯使用。属性按"缺陷条件（Fix Checking）"与"保持不变（Preservation）"组织，并回链到 `bugfix.md` 的 Expected Behavior（2.x）与 Regression Prevention（3.x）子句。

Property 1: Bug Condition A — 初始化顺序安全（Init-Order Safety）

_For any_ 静态初始化/插件初始化—销毁序列，当 `isBugCondition_A` 为真（存在跨 TU 全局构造次序依赖或跨生命周期裸存储指针）时，修复后的系统 SHALL 在**任意**链接/初始化顺序下都产生完整且正确的 OpenApi 文档与校验规则集，并使存储与依赖它的服务之间的生命周期关系显式且被保证（存储先构造、后析构，或以共享所有权表达）。

**Validates: Requirements 2.1, 2.2, 2.3**

Property 2: Bug Condition B — 线程安全 / 无数据竞争（Data-Race Freedom）

_For any_ 并发访问时序，当 `isBugCondition_B` 为真（并发读写无同步保护的共享可变状态）时，修复后的系统 SHALL 不产生数据竞争（在 ThreadSanitizer 下无 warning），且并发结果与某个合法的串行执行结果一致（`rules_`/`publicPaths_` 恰好初始化一次；`JwkManager` 读到完全初始化的一致密钥；任何共享计数无丢失）。

> **范围说明**：`CachedOAuth2Storage` 的 `CacheMap` 访问**不在** Property 2 内——`CacheMap` 自带互斥锁，跨线程访问无竞争，故**不期望对其有 TSan 数据竞争报告**（参见 Property 3 / 1.6 的生命周期归类）。另需说明：缓存采用 look-aside 模式，N 个并发未命中会触发 N 次 DB 回源（cache stampede，缓存踩踏）；这些回源最终收敛到相同的值，但**并非严格线性一致**——这是良性的性能特征，不是内存安全或正确性缺陷。

**Validates: Requirements 2.4, 2.5, 2.7**

Property 3: Bug Condition C — 异步回调生命周期安全（No Use-After-Free）

_For any_ "对象先于其异步回调被销毁"的时序，当 `isBugCondition_C` 为真（回调捕获裸 `this`/裸指针且对象可能先销毁）时，修复后的系统 SHALL 不发生 use-after-free（在 AddressSanitizer 下无 heap-use-after-free），回调要么安全持有对象并完成、要么在对象已失效时安全跳过，绝不经悬垂指针访问已析构成员（含 `CachedOAuth2Storage` 的 `tokenCache_/clientCache_` 这类 `CacheMap` 成员，以及 `RedisOAuth2Storage`/`PostgresOAuth2Storage` 在直连路径下的成员）。

**Validates: Requirements 2.6, 2.8, 2.9, 2.10, 2.11**

Property 4: Preservation — 正常路径行为不变（Behavioral Equivalence on ¬C(X)）

_For any_ 不触发上述任一缺陷条件的输入/时序（¬C(X)：顺序初始化、运行期只读、对象存活期内回调、已正确同步的访问），修复后的系统 SHALL 产生与现状（F）完全相同的结果——相同的 API 文档与校验规则、相同的核心流程响应/错误码/JSON 结构、相同的 id_token/JWKS、相同的缓存语义、相同的清理锁语义、相同的 RBAC 判定。

**Validates: Requirements 3.1, 3.2, 3.3, 3.4, 3.5, 3.6, 3.7**

## Fix Implementation

> 再次强调：以下为**修复方案设计**，本阶段**不修改源码**。每条给出根因、建议修复方式、取舍、回归防护，以及与「## Async Callback Lifetime Safety — 库对照与推荐模式」一节推荐模式的对应关系。代码片段为**示意性伪代码/草案**，仅用于说明方向，不代表最终实现。

### 类别 A —— 静态/全局初始化顺序与相互依赖

#### 1.1 OpenApi 文档注册的 SIOF（`OAuth2StandardController.cc` ↔ `OpenApiGenerator`）

**文件/符号**：`OAuth2Plugin/src/controllers/OAuth2StandardController.cc`（命名空间内 `OAuth2StandardControllerDocs docs_;` + `initApiDocs()`）；`OpenApiGenerator`（函数内静态 `endpoints/apiInfo/initialized/serverConfig`）。

**根因**：用文件作用域全局对象 `docs_` 的**构造副作用**去写另一个 TU 的全局注册表，两者构造次序跨 TU 未定义（SIOF）。

**建议修复方式（首选 → 备选）**：
1. **首选——显式启动期注册**：删除 `docs_` 这一"靠构造副作用注册"的全局对象，改为在 `OAuth2Plugin::initAndStart()`（或专门的 `registerApiDocs()`）中**显式调用** `OAuth2StandardController::initApiDocs()`。此时 `OpenApiGenerator` 的函数内静态已可被首次访问安全初始化（Meyers Singleton 语义），不再依赖全局构造次序。
2. **备选——首次访问即初始化**：若必须保留"被动注册"，把 `OpenApiGenerator` 的注册表全部收敛为函数内静态访问器（`getEndpoints()` 返回 `static std::vector<...>&`），并确保所有写入都经访问器；`docs_` 仍存在但其写入目标改为"首次访问即构造"的局部静态，规避未初始化读。

**取舍**：
- 方案 1 把注册从"隐式自动"变为"显式调用"，需要保证 `initAndStart` 一定执行一次（插件生命周期已保证）；优点是顺序完全确定、可测试、易调试。
- 方案 2 改动面小、保留自动注册习惯，但"被动注册 + 全局对象"模式仍较脆弱，且多个 TU 各自的 `docs_` 仍是无序构造（只是不再读未初始化数据）。
- 推荐方案 1：与"显式在插件启动阶段初始化"的全局策略一致。

**回归防护（3.1）**：注册的端点集合、参数、响应示例内容必须与现状逐字一致。验证手段：对 `/.well-known/openid-configuration`、JWKS、各端点文档做快照对比（修复前后 diff 为空）。

#### 1.2 `OAUTH2_VALIDATION_RULES` 文件作用域非平凡全局 + 运行期 `call_once` 填充

**文件/符号**：`OAuth2Plugin/src/filters/RequestValidationFilter.cc`（`std::map<...> RequestValidationFilter::OAUTH2_VALIDATION_RULES;` + `initializeValidationRules()` + `getValidationRules()` 内的 `std::call_once`）。

**根因**：文件作用域非平凡全局 `std::map` 的构造次序未定义；其填充依赖运行期 `call_once`，"构造时机"与"填充时机"分离。

> **澄清（避免与类别 B 混淆）**：`RequestValidationFilter::getValidationRules` 本身**已经是线程安全的**（`call_once` 一次性填充 + 运行期只读 + 返回副本）。1.2 的问题**纯粹是 SIOF（类别 A）**——文件作用域全局对象的跨 TU 构造次序未定义，**不是**类别 B 的数据竞争。下述修复只针对 SIOF。

**建议修复方式**：
1. **把全局 `std::map` 改为函数内静态访问器**（Meyers Singleton）：
   ```
   // 示意：构造 + 填充合一，首次访问即完成，线程安全（C++11 起函数局部静态初始化有保证）
   static const std::map<std::string, RouteValidationRules>& rules() {
       static const std::map<std::string, RouteValidationRules> kRules = buildRules();
       return kRules;
   }
   ```
   `buildRules()` 返回完整 map，去掉独立的 `call_once`/`initFlag`，因为函数局部静态的首次初始化本身就是线程安全且仅一次（满足 2.2）。
2. `getValidationRules()` 改为读 `rules()`，逻辑不变。

**取舍**：
- 函数内静态把"构造 + 一次性填充"合并为标准保证的线程安全初始化，既消除 SIOF 又消除 check-then-fill 的时机分离；代价是规则集变为不可变（const），如需热更新需另设机制（当前无此需求）。
- 保留现有 `call_once` 也能保证"填充一次"，但**不能**解决"全局对象本身构造次序未定义"的根因，故不推荐只保留 `call_once`。

**回归防护（3.1、3.7）**：`getValidationRules(path)` 对相同 path 返回的规则（字段、min/max、pattern、enabled）必须与现状一致；校验放行/拒绝结果不变。

#### 1.3 `storage_` 与各服务之间的裸指针生命周期约定（`OAuth2Plugin`）

**文件/符号**：`OAuth2Plugin/include/oauth2/plugin/OAuth2Plugin.h`（成员 `std::unique_ptr<IOAuth2Storage> storage_;` 与 `tokenService_/clientService_/identityService_/cleanupService_`）；`OAuth2Plugin.cc`（`initAndStart()` 构造顺序、`shutdown()` 的 `storage_.reset()`）；各服务构造函数接收 `IOAuth2Storage*`。

**根因**：服务持有 `storage_.get()` **裸指针**，"storage 必须先构造、后析构"是隐式约定，未由类型系统保证；`shutdown()` 显式 `storage_.reset()` 可在服务仍存活且可能有在途回调时先释放存储。

> **🔴 1.3 是 1.8 的硬性前置条件（BLOCKING PREREQUISITE）**：1.8 的修复依赖 `CachedOAuth2Storage::shared_from_this()`。当前 `storage_` 由 `std::unique_ptr<IOAuth2Storage>` 持有，对象**不在任何 `shared_ptr` 控制块下**，此时调用 `shared_from_this()` 会抛出 `std::bad_weak_ptr`。因此**必须先完成 1.3**（把 `storage_` 改为 `shared_ptr`），1.8 才可能成立。1.3 从"被提及的依赖"提升为**显式阻塞前置**。
>
> **⚠️ 关键接线注意（控制块必须绑定到具体派生类型）**：完成 1.3 时，`storage_` **必须**通过 `std::make_shared<CachedOAuth2Storage>(...)` 由**具体派生类型**创建，再隐式转换为 `shared_ptr<IOAuth2Storage>`。**切勿**把已有的 `unique_ptr<IOAuth2Storage>` `std::move` 进一个 `shared_ptr<IOAuth2Storage>`——那样控制块绑定到**基类** `IOAuth2Storage`，而 `enable_shared_from_this<CachedOAuth2Storage>` 的内部 `weak_this` 不会被正确武装（armed），`shared_from_this()` 仍会抛 `bad_weak_ptr`。正确做法示意：
> ```
> // 正确：控制块绑定到具体类型 CachedOAuth2Storage
> auto cached = std::make_shared<CachedOAuth2Storage>(std::move(impl), redisClient);
> storage_ = cached;                      // shared_ptr<IOAuth2Storage>，weak_this 已正确武装
>
> // 错误：控制块绑定到基类，shared_from_this() 抛 bad_weak_ptr
> std::unique_ptr<IOAuth2Storage> up = std::make_unique<CachedOAuth2Storage>(...);
> std::shared_ptr<IOAuth2Storage> bad = std::move(up);   // ✗
> ```
> **已确认**：各服务（`TokenService`/`ClientService`/`IdentityService`/`OAuth2CleanupService`）当前**已由 `make_shared` 持有**，故一旦为它们加上 `enable_shared_from_this` 基类，`shared_from_this()` 对它们即刻有效（1.9/1.10 不受此前置阻塞）。仅 `storage_`（1.8）需要先做 1.3 的所有权改造与上述接线。

**建议修复方式**：
1. **改为共享所有权**：把 `storage_` 从 `std::unique_ptr<IOAuth2Storage>` 改为 `std::shared_ptr<IOAuth2Storage>`；各服务（`TokenService`/`ClientService`/`IdentityService`/`OAuth2CleanupService`）改为持有 `std::shared_ptr<IOAuth2Storage>`（或 `weak_ptr`，见下）。这样只要任一使用者存活，存储就不被析构，消除"存储先于使用者释放"的竞态。
2. **明确销毁顺序**：`shutdown()` 中先 `cleanupService_->stop()`（停定时器），再释放服务，最后释放存储；配合 1.8/1.9/1.10 的 `shared_from_this`/`weak_from_this`，使在途回调要么持有存储 `shared_ptr` 安全完成、要么安全跳过。
3. **可选——`weak_ptr` + 用时 `lock()`**：若希望"插件关停后回调一律跳过而非延长存储寿命"，服务持 `weak_ptr<IOAuth2Storage>`，每次异步使用前 `lock()`，失败即安全返回。

**取舍**：
- `shared_ptr`（方案 1）最简单、最安全，代价是接口签名从裸指针改为 `shared_ptr`（需同步改 `IOAuth2Storage` 使用点与各服务构造函数；`getStorage()` 返回值语义需评估）。
- `weak_ptr`（方案 3）能保证"关停即停"，但每次调用多一次 `lock()` 开销与分支；与 1.8/1.9 的回调内 `lock()` 风格一致。
- 推荐：存储所有权用 `shared_ptr`（方案 1 + 2）；回调内对"自身对象"用 `weak_from_this`（见类别 C）。两者组合最稳。

**回归防护（3.2、3.4、3.5）**：所有核心流程在正常时序下结果不变；缓存语义不变；正常回调副作用不变。共享所有权不改变任何业务逻辑，仅改变析构时机保证。

### 类别 B —— 多线程线程安全

#### 1.4 `AuthorizationFilter::loadConfig()` 的并发 check-then-act

**文件/符号**：`OAuth2Plugin/src/filters/AuthorizationFilter.cc`（`loadConfig()` 中 `if (initialized_) return; ... rules_.push_back(...); publicPaths_.push_back(...); initialized_ = true;`）。

**根因**：多个 IO 线程首次进入时，对 `initialized_` 的"检查再写入"无锁，可同时通过检查并发写 `rules_`/`publicPaths_`。

**建议修复方式（首选 → 备选）**：
1. **首选——`std::call_once` + 每实例非静态 `once_flag` 成员**：用**类成员** `std::once_flag initFlag_`（**不要**用函数局部 `static std::once_flag`）+ `std::call_once(initFlag_, [this]{ loadRulesSafely(); })` 保证每个实例的规则加载体只执行一次，并发后续进入只读。
   ```
   // 头文件：std::once_flag initFlag_;  作为每实例成员
   void AuthorizationFilter::loadConfig() {
       std::call_once(initFlag_, [this]{ /* 见下方异常安全写法 */ });
   }
   ```
   > **⚠️ 为何必须用成员 `once_flag` 而非函数局部 `static`**：函数局部 `static std::once_flag` 在**所有实例间共享**，而 lambda 写的是 `this->rules_`/`this->publicPaths_`。若进程中曾存在多于一个 `AuthorizationFilter` 实例，则**只有第一个实例**会被填充，其余实例的规则保持为空——形成"静默空规则"缺陷。即便目前过滤器为单例，用每实例成员 `once_flag` 也更健壮、无此隐患。（备选：采用方案 2 在 `initAndStart` 期加载，运行期纯只读。）
   >
   > **⚠️ 异常安全（强保证）**：`std::regex(pattern)` 可能抛 `std::regex_error`，`push_back` 也可能抛。若初始化体在**填充中途**抛出，`call_once` **不会**消费该 flag（这是正确语义），但 `rules_`/`publicPaths_` 会处于**部分填充**状态，且下次重试会**重复追加**规则。修复方式：在**局部 vector** 中构建完整结果，全部成功后再 `swap` 进成员（强异常保证）：
   > ```
   > std::vector<Rule> localRules;
   > std::vector<std::string> localPublic;
   > /* 构建 localRules / localPublic；任意抛出都不污染成员 */
   > rules_.swap(localRules);
   > publicPaths_.swap(localPublic);   // 仅在全部成功后整体提交
   > ```
   > **⚠️ 不要保留非原子快路径**：**务必删除** `call_once` 之前的 `if (initialized_) return;` 非原子快路径——该读仍与初始化体内的写竞争。`call_once` 自带高效快路径，无需手写；若确需保留 `initialized_` 标志，必须将其改为 `std::atomic<bool>`。
2. **更佳——启动期加载**：把规则加载移到 `OAuth2Plugin::initAndStart()`（请求开始前），运行期 `doFilter` 只读 `rules_`/`publicPaths_`，彻底避免运行期并发初始化。
3. 备选：`std::mutex` 保护，但 `call_once`/启动期加载更轻量、语义更清晰。

**取舍**：
- `call_once` + 每实例 `once_flag` 成员（方案 1）改动最小、贴合"恰好一次"语义；首请求路径上一次性同步开销可忽略；用成员 flag 规避了"多实例只填第一个"的隐患。
- 启动期加载（方案 2）最干净（运行期纯只读，零同步开销），但要求过滤器能在 `initAndStart` 阶段拿到配置（`app().getCustomConfig()` 在该阶段可用），并需确认过滤器实例与加载时机的衔接。
- 推荐方案 1 为最小改动落地，方案 2 为更优目标态。

**回归防护（3.7）**：加载后的 `rules_`/`publicPaths_` 内容与现状一致；`checkAccess` 的放行/401/403 行为不变。

#### 1.5 `JwkManager` 的初始化写 / 运行期读竞争

**文件/符号**：`OAuth2Plugin/src/utils/JwkManager.cc`（`init()` 写 `rsaKey_/initialized_/kid_`；`signJwt()/getJwks()` 读）；`OAuth2Plugin.cc`（`jwkManager_->init(...)`、`tokenService_->setJwkManager(...)`）。

**根因**："启动期初始化、运行期只读"的 happens-before 假设未文档化、未由代码强制；存在非 const 变更入口（`init()`、外部 `setJwkManager()`）。

**建议修复方式**：
1. **强制 init-once-then-read-only 契约**：
   - 在 `OAuth2Plugin::initAndStart()` 中**在启动接受请求前**完成 `jwkManager_->init(...)`（现状已是如此），并以**文档 + 断言**固化："`init()` 仅在 `initAndStart` 期间调用一次；此后 `signJwt()/getJwks()` 只读。"
   - `signJwt()/getJwks()` 已是 `const`；进一步把 `init()` 设计为"仅可调用一次"（内部 `initialized_` 守卫，重复调用记录错误并 no-op），消除运行期再变更可能。
2. **更强——不可变发布（immutable post-init）**：让 `OAuth2Plugin` 持有 `std::shared_ptr<const JwkManager>`：先在本地构造并 `init()`，完成后以 `shared_ptr<const JwkManager>` 形式发布给 `tokenService_` 等。`const` 指针从类型上禁止运行期变更。
3. **happens-before 的内存可见性（措辞精确化）**：Drogon 的 IO worker 线程**通常在 `initAndStart()` 运行之前就已存在**（线程池预先创建），因此"线程启动即同步边"的说法**不精确**。真正的 acquire 边是**第一个请求经事件循环队列被同步发布（publish）到 worker 线程**这一动作——`queueInLoop`/事件循环投递构成 release→acquire 配对。`shared_ptr<const>` 仅保证此后不再发生变更，但其本身**不会**自动建立 happens-before，除非该发布动作发生在并发开始之前。结论：只要 `init()` 在"开始接受请求并向事件循环投递任务"之前完成，运行期只读即无竞争、无需逐次加锁。

#### OpenSSL 并发性（1.5 子项：`signJwt` 的底层密钥对象）

`signJwt()` 在并发签名时，所有调用共享**同一个** `EVP_PKEY*`（`rsaKey_`）。需要明确：

- **已正确的部分**：`signJwt()` 中 `EVP_MD_CTX` 是**每次调用** `EVP_MD_CTX_new()` 创建、`EVP_MD_CTX_free()` 释放的——**这正是并发签名安全的关键**。每个线程持有独立的 `EVP_MD_CTX`，不存在共享的签名上下文；唯一被并发共享的可变对象是 `EVP_PKEY`。
- **`EVP_PKEY` 的并发安全前提**：并发签名会触及 `EVP_PKEY` 的内部状态（引用计数、`BN_BLINDING`、惰性初始化的 `BN_MONT_CTX`）。这些在 **OpenSSL >= 1.1.0** 上是线程安全的（原子引用计数 + 自动初始化的内部锁）；在 **1.0.2 及更早**版本上则需应用注册 legacy 的 `CRYPTO_set_locking_callback`/`CRYPTO_THREADID` 回调，否则并发签名是数据竞争。**本设计明确假设 OpenSSL >= 1.1.0（且启用线程支持，threads-enabled）**；若项目可能链接 1.0.2，须在实现阶段补充 legacy 锁回调或升级 OpenSSL。
- **弃用迁移提示**：`getPublicKeyComponents()` 使用的 `EVP_PKEY_get1_RSA` 在 **OpenSSL 3.0 已弃用**，建议迁移到 `EVP_PKEY_get_bn_param(pkey, OSSL_PKEY_PARAM_RSA_N/E, ...)` 等 3.0 API（属增强/迁移项，非本次安全必需）。

**取舍**：
- 方案 1（契约 + 断言 + 一次性 init）改动最小，依赖"启动期完成初始化"的既有结构，足以消除竞争，但仍靠纪律约束。
- 方案 2（`shared_ptr<const>` 不可变发布）最稳健，类型层面禁止变更，代价是 `setJwkManager` 等签名调整为传 `shared_ptr<const JwkManager>`。
- 推荐方案 2 为目标态；若改动受限，至少落地方案 1。

**回归防护（3.3）**：相同密钥、`kid`、RS256 算法，签发一致的 id_token/JWKS。修复只改并发可见性保证，不改签名逻辑。

#### 1.6 `CachedOAuth2Storage` 的 `CacheMap` 成员生命周期（已重归类为类别 C，并入 1.8）

> **🔴 重大修订（前提推翻）**：原草案把 1.6 当作"`CacheMap` 跨 loop 数据竞争"处理，**该前提经复核确认错误**。`drogon::CacheMap` 内部以 `std::mutex mtx_`（保护 `map_`）与 `bucketMutex_`（保护时间轮）加锁，所有公共操作均在锁内执行，`findAndFetch` 在锁内返回副本，`modify()` 文档标注多线程安全；构造传入的 `EventLoop*` **仅驱动周期清理定时器，非访问亲和约束**。因此**从 Redis/DB 回调线程跨 loop 调用 `tokenCache_/clientCache_` 是安全的，不是数据竞争**。

**正确的残留关注点（生命周期）**：`tokenCache_/clientCache_` 是 `CachedOAuth2Storage` 的**成员**。若宿主对象在异步回调途中被 `storage_.reset()` 析构，这两个 `CacheMap` 成员随之析构，而在途回调仍经悬垂 `this` 访问它们 → use-after-free。**这与 1.8 完全同源**，因此：

- **本条并入 1.8（类别 C）统一处理**：用 `enable_shared_from_this` + 回调捕获 `self`（`shared_ptr`），让宿主对象（及其 `CacheMap` 成员）在回调完成前不被析构即可，无需任何针对 `CacheMap` 的特殊编排。
- **明确放弃 `queueInLoop` 编排方案**：原草案建议把缓存访问 `queueInLoop` 回 `app().getLoop()`——此方案**既无必要**（`CacheMap` 本身线程安全），**又会回归**：它会把 `getClient`/`getAccessToken` 中"L1 命中即**同步**回调"的控制流强行改为"总是经由 loop 异步"，破坏 Preservation 3.4/3.5（缓存命中路径的同步控制流与回调时序语义）。故**不采用**。
- **唯一良性残留**：look-aside 模式下 N 个并发未命中会触发 N 次 DB 回源（cache stampede）。这些回源收敛到相同值，属良性性能特征，**非内存安全或正确性缺陷**；如需可后续以单飞（single-flight）合并请求优化，但不在本安全审计范围内。

> **版本核对提醒**：上述 `CacheMap` 线程安全结论对照 **upstream master** 源码；**实现阶段须对照项目锁定的 Drogon 版本再次确认**。

**回归防护（3.4、3.5）**：由 1.8 的 `shared_from_this` 修复覆盖——对象存活期内缓存命中/未命中/失效/TTL 行为及同步回调时序与现状逐位一致；仅在"对象本应被销毁但有在途回调"的竞态下，从 UAF 变为安全完成。**注意：不引入任何会改变 L1 命中同步控制流的编排。**

#### 1.7 `OAuth2Metrics` 的潜在共享计数竞争

**文件/符号**：`OAuth2Plugin/src/observability/OAuth2Metrics.cc`（当前各 `incXxx`/`observeLatency` 仅 `LOG_INFO`，注释自承"松一致性"）。

**根因**：当前无共享计数器（无真实竞争，属潜在缺陷）；一旦引入普通（非原子）共享计数自增即成无同步竞争点。

**建议修复方式**：
1. **预防性设计约定**：在引入任何进程级共享计数前，规定**必须**使用 `std::atomic<...>`（或成熟指标库的线程安全计数器，如 Prometheus client 的 `Counter`/`Gauge`，本仓库 `config.json` 已含 `PromExporter`）。
2. 若沿用现有"日志即指标（log-based metrics）"则保持现状（`LOG_INFO` 本身线程安全），不引入共享可变计数；这是零竞争的最小风险路径。
3. 文档化：在 `OAuth2Metrics` 头部注释把"松一致性"替换为明确契约——"计数必须原子；不得引入非原子共享可变状态"。

**取舍**：
- 因当前无真实竞争，本条优先级最低，属"防止将来引入缺陷"的护栏。
- 用 `std::atomic` 计数足以满足无丢失；若要带标签的多维指标，宜直接接入 `PromExporter`，避免自造易错轮子。

**回归防护（3.2 间接）**：可观测性不改变业务响应；本条仅约束未来计数实现，确保不引入竞争或计数丢失（2.7）。

### 类别 C —— 异步回调的对象生命周期安全

> 类别 C 的统一修复模式：**让带异步回调的对象继承 `std::enable_shared_from_this<T>`，并以 `shared_ptr`（延长生命周期）或 `weak_ptr`（用时 `lock()`，对象已销毁则安全跳过）捕获自身，替换裸 `this`/裸指针。** 具体采用 shared 还是 weak，依"回调是否必须完成副作用"而定（见各条）。

#### 1.8 `CachedOAuth2Storage`（及直连存储）异步回调中的悬垂 `this`（含原 1.6）

**文件/符号**：
- `OAuth2Plugin/src/storage/CachedOAuth2Storage.cc`（`getAccessToken`/`saveAccessToken`/`revokeAccessToken` 等捕获 `[this,...]` 传入 `redisClient_->execCommandAsync(...)` 与 `impl_->...`）；`CachedOAuth2Storage.h`（类声明）。
- **`OAuth2Plugin/src/storage/RedisOAuth2Storage.cc`**（`revokeAccessToken`、`atomicRevokeRefreshToken` 在 `execCommandAsync` 回调捕获裸 `[this]`）；`RedisOAuth2Storage.h`。
- **`OAuth2Plugin/src/storage/PostgresOAuth2Storage.cc`**（`revokeAccessToken` 在异步回调捕获裸 `[this]`）；`PostgresOAuth2Storage.h`。

**根因**：上述类均未继承 `enable_shared_from_this`，回调捕获裸 `this`；销毁时序（`shutdown()` 的 `storage_.reset()`，或直连模式下底层存储被释放）可在回调前析构对象 → 经悬垂 `this` 访问 `tokenCache_/clientCache_/redisClient_`（缓存装饰层）或底层存储的成员。

> **覆盖范围必须包含直连路径**：`RedisOAuth2Storage`/`PostgresOAuth2Storage` 在以下路径**没有外层 `CachedOAuth2Storage` 包裹**保护，必须独立修复——
> - **redis 模式**：`RedisOAuth2Storage` 被直接用作 `IOAuth2Storage` 实现（`revokeAccessToken`/`atomicRevokeRefreshToken` 直连）。
> - **postgres 无缓存回退路径**：`PostgresOAuth2Storage::revokeAccessToken` 直连。
>
> **原 1.6 并入本条**：`tokenCache_/clientCache_` 是 `CachedOAuth2Storage` 的成员；`CacheMap` 本身线程安全，无需特殊编排，捕获 `self` 保证宿主存活即同时保护了这两个成员。

**建议修复方式**：
1. 让 `CachedOAuth2Storage`、`RedisOAuth2Storage`、`PostgresOAuth2Storage` 各自继承 `std::enable_shared_from_this<T>`（理想情况下整条 `IOAuth2Storage` 装饰/实现链）。
2. 所有异步 lambda 由 `[this, ...]` 改为捕获 `auto self = shared_from_this();`（`[self, this, ...]` 或仅 `[self, ...]` 并经 `self->` 访问成员），使回调在执行期间持有对象，**保证成员（含 `CacheMap`、`redisClient_`、DB 句柄）在回调完成前不被析构**。
3. **原 1.6 协同（修正）**：`CacheMap` 自身线程安全，**不需要** `queueInLoop` 编排；只需让回调持有 `self` 即可保护 `tokenCache_/clientCache_` 这两个成员的生命周期。**不得**引入会改变 L1 命中同步控制流的 loop 编排（否则回归 3.4/3.5）。

> **🔴 `shared_from_this` 接线硬前置（依赖 1.3）**：`CachedOAuth2Storage` 当前由 `unique_ptr<IOAuth2Storage>` 持有，**此刻调用 `shared_from_this()` 会抛 `std::bad_weak_ptr`**。必须**先完成 1.3**（`storage_ → shared_ptr`），且按 1.3 的接线注意——经 `std::make_shared<CachedOAuth2Storage>(...)` 由**具体派生类型**创建控制块，**不可**把 `unique_ptr<IOAuth2Storage>` move 进 `shared_ptr<IOAuth2Storage>`（控制块绑定基类会使 `shared_from_this()` 仍抛 `bad_weak_ptr`）。`RedisOAuth2Storage`/`PostgresOAuth2Storage` 作为直连实现时，同样需确保它们由 `make_shared` 创建并以 `shared_ptr` 持有。

> **🔴 嵌套所有权推论（NESTED ownership — `CachedOAuth2Storage::impl_` 内层存储）**：`PostgresOAuth2Storage`/`RedisOAuth2Storage` 在本仓库**同时扮演两种角色**，二者的 `shared_from_this()` 有效性**不同**，必须区别对待：
> - **(a) 独立/直连角色**（redis 模式、postgres 无缓存回退）：由 `storage_` 经 `make_shared<具体类型>` **直接持有**（`shared_ptr`）→ 其自身的 `shared_from_this()` **有效**。
> - **(b) 被包裹角色**（作为 `CachedOAuth2Storage::impl_`）：`CachedOAuth2Storage` 以 `std::unique_ptr<IOAuth2Storage> impl_` **独占持有**内层存储 → 该内层对象**不在任何 `shared_ptr` 控制块下**，此时调用**它自己的** `shared_from_this()` 会抛 `std::bad_weak_ptr`（与外层同源的失败模式，只是**下沉了一层**）。
>
> **⚠️ 因此：一个"让三个类都继承 ESFT 并无条件调用 `shared_from_this()`"的朴素实现，会在缓存/包裹路径下崩溃（`bad_weak_ptr`）。** 必须在以下两个方案中择一并**一致地**应用：
> - **方案 A（首选）**：被包裹路径**不依赖内层对象自己的 `shared_from_this()`**。改由**外层 `CachedOAuth2Storage` 捕获自身的 `self`（`shared_ptr`）**——由于外层独占持有 `impl_`，`self` 存活即**传递性地**保证 `impl_` 在回调期间存活；因此内层 Postgres/Redis 的异步回调**由外层 `self` 保护，无需内层自备 ESFT**。（内层仅在直连角色 (a) 下才用自己的 `shared_from_this()`。）
> - **方案 B**：把 `CachedOAuth2Storage::impl_` 改为 `std::shared_ptr<IOAuth2Storage>`，并经 `make_shared<具体类型>(...)` 创建 → 内层对象的 `shared_from_this()` 在被包裹路径下**也有效**，内层回调可统一自捕获 `self`。
>
> 无论选 A 还是 B，都必须**全链路一致**：不可在被包裹路径上让内层对象调用自己未武装（unarmed）的 `shared_from_this()`，否则即 `bad_weak_ptr` 崩溃。

**取舍**：
- 捕获 `shared_ptr self`（延长寿命）能保证写穿/失效等副作用完成，语义最稳；代价是关停时析构会被在途回调短暂延后。
- 若希望关停即停、丢弃在途副作用，可改捕获 `weak_ptr` 并在回调入口 `lock()`；但缓存写穿类副作用中途丢弃可能导致 L2 与 DB 短暂不一致，故此处**推荐 `shared_ptr`**。
- 前置条件：对象必须由 `shared_ptr` 管理（依赖 1.3 的所有权改造与接线）。

> **⚠️ 关停（shutdown）与 `self` 自捕获的交互**：捕获 `shared_ptr self` 会把 `~CachedOAuth2Storage`（及底层 `RedisOAuth2Storage`/`PostgresOAuth2Storage`）的析构**延后到最后一个在途回调完成**，而该回调运行在 **redis/DB 的 loop 线程**上——意味着析构发生在那个 loop 线程，而非发起 `shutdown()` 的线程。原草案仅称此为"短暂延后（可接受）"，但还须考虑**析构线程**与 **Drogon 关停序列**的交互：必须保证在这些延后析构发生时，对应的 redis/DB loop **仍在运行**（否则回调永不触发、`self` 永不释放，或在 loop 已停后触发未定义行为）。建议：在 `OAuth2Plugin::shutdown()` 中**先排空（drain）在途回调 / 等待在途操作完成，再 `storage_.reset()`**，或确保 `storage_.reset()` 与各客户端 loop 的停止顺序使延后析构落在 loop 存活窗口内。此点须对照 Drogon 的关停序列在实现阶段确认。

**回归防护（3.4、3.5）**：对象存活期内（绝大多数正常路径）行为与现状逐位一致，**包括 L1 命中的同步回调时序**；仅在"对象本应被销毁但有在途回调"的竞态下，从 UAF 变为"安全完成"。

#### 1.9 `TokenService`/`IdentityService` 异步链中的悬垂 `this`

**文件/符号**：`OAuth2Plugin/src/services/TokenService.cc`（`exchangeCodeForToken`/`refreshAccessToken` 多层 `[this,...]` 透传到 `storage_->...`）；`OAuth2Plugin/src/services/IdentityService.cc`（`ensureSubjectMapping`/`handleFirstTimeLogin`/`validateUserRolesForScopes` 等）。

**根因**：两类均未继承 `enable_shared_from_this`，多层异步 lambda 捕获裸 `this`，访问 `storage_/accessTokenTtl_/jwkManager_` 等；服务先于回调销毁则 UAF。

**建议修复方式**：
1. 让 `TokenService`、`IdentityService`（以及同源的 `ClientService`）继承 `std::enable_shared_from_this<...>`。**已确认**：这些对象在 `OAuth2Plugin` 中**已由 `make_shared` 创建并以 `shared_ptr` 持有**，因此控制块已正确绑定到各自的具体类型——一旦加上 `enable_shared_from_this` 基类，`shared_from_this()` 即刻有效，**不存在 1.8 那种 `bad_weak_ptr` 接线陷阱**（无需先做所有权改造）。
2. 每条异步链最外层捕获 `auto self = shared_from_this();`，内层 lambda 透传 `self`（而非 `this`），成员访问经 `self->`。对深层嵌套链，逐层透传同一个 `self`（或在每层重新 `shared_from_this()`）。
3. 与 1.5 协同：`jwkManager_` 改为 `shared_ptr<const JwkManager>`，回调内经 `self->jwkManager_` 安全读取。
4. 与 1.3 协同：`storage_` 改 `shared_ptr` 后，回调持 `self` 即间接保证 `storage_` 存活；如采用 `weak_ptr<storage>` 路线，则回调内 `lock()` 检查。

**取舍**：
- 捕获 `shared_ptr self` 保证令牌签发/刷新等关键副作用完成，语义最稳；多层链中需注意每层都带上 `self`，避免某层退回裸 `this`。
- `TokenService` 中存在像 `OAuth2Plugin::validatePkceCodeVerifier` 通过 `oauth2::TokenService(nullptr)` 临时对象调用纯函数的用法——这类**无异步、无成员依赖**的纯静态用法不在 C(X) 内，无需改造（属 ¬C(X)，保持不变）。

**回归防护（3.2、3.5）**：核心令牌流程在正常时序下响应/错误码/JSON 结构不变；正常回调副作用（入库、审计日志）不变。

#### 1.10 `OAuth2CleanupService::runCleanup()` Redis 回调的悬垂 `this`（与 `start()` 不一致）

**文件/符号**：`OAuth2Plugin/src/plugin/OAuth2CleanupService.cc`（`runCleanup()` 中 `redis->execCommandAsync([this]{...storage_->deleteExpiredData();...})`；对照 `start()` 已用 `std::weak_ptr<...> weakSelf = weak_from_this();`）；类已继承 `enable_shared_from_this`（见 `OAuth2CleanupService.h`）。

**根因**：类**已具备** `enable_shared_from_this`，`start()/runEvery` 已正确用 `weak_from_this()`，但 `runCleanup()` 的 Redis 成功/失败回调仍捕获裸 `this` 调用 `storage_->deleteExpiredData()`；服务在命令派发与回复间销毁则 UAF。这是**实现不一致**，修复方向最明确。

**建议修复方式**：
1. 在 `runCleanup()` 取 `auto weakSelf = weak_from_this();`，两个回调（成功 lambda 与异常 lambda）均捕获 `weakSelf`，入口 `auto self = weakSelf.lock(); if (!self || !self->running_) return;`，再经 `self->storage_->deleteExpiredData()`。
2. 与 `start()` 完全对齐，形成一致的"`weak_from_this` + `lock()` 守卫"模式。
3. 这里**推荐 `weak_ptr`（而非 `shared_ptr`）**：清理任务是周期性、可丢弃的——若服务正在关停，跳过本次清理是正确行为，无需延长寿命。

**取舍**：
- `weak_ptr` + `lock()` 与既有 `start()` 风格一致，关停即安全跳过，符合清理语义；几乎零额外成本。
- 不宜用 `shared_ptr` 延长寿命，否则关停时清理仍可能跑，违背"关停即停"直觉（也与 `stop()` 的 `running_=false` 守卫意图一致）。

**回归防护（3.6）**：分布式锁可用/不可用两种场景下的"获锁才清理 / 无锁单实例清理"语义不变；仅在"服务已销毁"竞态下安全跳过。

#### 1.11 `OAuth2StandardController` 长链异步中的裸 `this`

**文件/符号**：`OAuth2Plugin/src/controllers/OAuth2StandardController.cc`（`introspect`/`revoke`/`authorize` 等链路里 `[plugin, ...]`、底层经 `plugin->...` 透传；控制器方法内多层异步回调）。

**根因**：控制器为 Drogon 进程级单例（长生命周期），实际崩溃风险较低，但裸 `this`（及对单例的隐式依赖）成为隐式风险面，缺乏显式、统一的生命周期策略。**更要紧的是存储指针**：控制器通过 `getStorage()->...` 跨异步持有**裸存储指针**（如 `client_credentials` 授权、`userinfo` 等链路）；存储对象的生命周期**不**受控制器单例保护。

> **⚠️ 1.3 单独不足以修复存储指针风险**：把 `storage_` 改为 `shared_ptr`（1.3）**不会**自动让控制器持有的存储指针变安全。只有当 **`getStorage()` 的两个重载都返回 `shared_ptr<IOAuth2Storage>`**（而非裸指针/引用），**且**控制器的异步链**捕获该 `shared_ptr`**（沿链透传，而非取 `.get()` 裸指针）时，存储生命周期才在异步期间被保证。实现 1.11 时必须同时满足这两点。

**建议修复方式**：
1. **文档化框架保证**：在控制器头部明确注释"Drogon 控制器为进程级单例，生命周期覆盖整个运行期；异步回调期间 `this` 始终有效"，把隐式约定转为显式契约（满足 2.11 的"显式且一致"）。
2. **存储指针共享化（关键）**：让 `getStorage()` 的**两个重载均返回 `shared_ptr<IOAuth2Storage>`**，控制器异步链捕获该 `shared_ptr` 并沿链透传，使存储在异步期间始终存活（与 1.3、1.8 协同）。
3. **统一捕获风格**：当前链路多捕获 `plugin`（`getPlugin<OAuth2Plugin>()` 返回的指针）与局部变量，而非裸 `this` 成员访问——保持这种"捕获所需局部 + 插件指针"的风格，并避免在回调中捕获裸 `this` 去访问控制器可变成员（控制器应保持无可变共享状态）。
4. **可选**：对确实需要访问控制器成员的异步链，统一用静态成员函数 + 显式参数传递，进一步消除对 `this` 的依赖。

**取舍**：
- 由于单例保证，本条**优先级最低**，以"文档化 + 风格统一"为主，不强制结构性改造，避免过度工程。
- 若未来控制器引入可变成员状态，则需升级为类别 C 的 `shared_ptr`/`weak_ptr` 模式或显式同步——本设计将其标记为"需复核的风险面"。

**回归防护（3.2）**：控制器端点响应/错误码/JSON 不变；本条主要是注释与风格约束，不改变运行时行为。

## Async Callback Lifetime Safety — 库对照与推荐模式

> 本节是用户明确要求的**异步回调生命周期安全的库对照**。先讲清 Drogon 自身如何处理，再对照 Boost.Asio / folly / seastar，最后给出本代码库**项目级统一模式**建议。
>
> 工作区未内置 Drogon 源码（`libs/drogon` 不存在），故 Drogon 部分引用的是**公认惯用法与公开 API 语义**；若后续纳入源码，应以实际实现复核。

### 1) Drogon 自身如何处理这些问题

Drogon（网络层基于 trantor）的并发模型与生命周期惯用法可概括为：

- **多 EventLoop + loop 亲和**：Drogon 以多个 IO 线程运行，每个连接、`DbClient`、`RedisClient` 绑定到某个 `EventLoop`。其异步回调在所属 loop 线程上执行。跨线程操作"属于某 loop 的资源"时，应把操作编排回该 loop。
- **`EventLoop::queueInLoop` / `runInLoop`**：把可调用对象投递到目标 loop 线程执行。`runInLoop` 在"当前线程即目标 loop"时**同步执行**、否则入队；`queueInLoop` **总是入队**。这是 Drogon 处理"**真正** loop 亲和资源跨线程访问"的标准手段。**注意**：`drogon::CacheMap` **不属于**此类——它内部自带互斥锁，可在任意线程安全访问，**无需** `queueInLoop` 编排（本设计据此**撤销**了原草案对 1.6 的 `queueInLoop` 方案）。
- **loop 绑定的 `CacheMap`（澄清）**：`drogon::CacheMap` 构造时接收一个 `EventLoop*`，但该指针**仅用于驱动其内部定时过期清理**，**不是**访问亲和约束。其读写由内部 `std::mutex mtx_` / `bucketMutex_` 保护，可在任意线程（含 Redis/DB 回调线程）安全调用，`findAndFetch` 在锁内返回副本，`modify()` 多线程安全。因此本仓库 `CachedOAuth2Storage` 在 Redis/DB 回调线程访问 `tokenCache_/clientCache_` **本身是安全的**；1.6 的真实问题是这些 `CacheMap` **成员**随宿主对象析构而产生的生命周期问题（并入 1.8）。（结论对照 upstream master，须按项目锁定版本复核。）
- **`enable_shared_from_this` / `weak_from_this`**：Drogon 中带异步回调、生命周期可能短于回调的对象（典型如 `HttpController` 之外的业务对象、定时任务持有者）惯用 `enable_shared_from_this`，在回调里捕获 `shared_ptr`（延长寿命）或 `weak_ptr`（用时 `lock()`，已销毁则跳过）。本仓库 `OAuth2CleanupService` 已在 `start()` 中正确使用 `weak_from_this()`——这正是 Drogon 惯用法的体现；缺陷 1.10 只是没在 `runCleanup()` 沿用它。
- **`runEvery` / `runAfter` 定时器**：`EventLoop::runEvery(interval, cb)` 在 loop 上周期执行回调，返回 `timerId`，用 `invalidateTimer(id)` 取消。配合 `weak_from_this()` 捕获，可在对象销毁后让定时回调安全 no-op——`OAuth2CleanupService::start()` 即此模式。
- **`DbClient` / `RedisClient` 回调线程模型**：异步 DB/Redis 操作的回调在客户端所属 loop 线程触发，**不保证**与发起请求的 loop 相同。因此回调里若要触碰**可能已销毁的发起对象**，必须 `shared_from_this`/`weak_from_this`。（注意：触碰 `CacheMap` **不**需要 `queueInLoop`，因其线程安全；本仓库 1.8/1.9/1.10 源于忽略了"回调可能晚于对象析构"的生命周期语义，而非跨线程访问本身。）

> 小结：本设计的所有推荐（`enable_shared_from_this` 捕获、`weak_from_this` 定时回调、init-once 只读）都**已是 Drogon 的原生惯用法**，仓库内 `OAuth2CleanupService::start()` 就是现成正例，修复本质是把这套惯用法一致地推广到所有异步入口。**唯一被撤销的原草案建议**是对 `CacheMap` 的 `queueInLoop` 编排——经复核 `CacheMap` 线程安全，该编排既无必要又会回归 L1 命中的同步控制流。

### 2) 与其他成熟 C++ 网络/异步库对照

**Boost.Asio**
- **strand（串行化执行器）**：`asio::strand` 保证投递到同一 strand 的处理器不并发执行——等价于"逻辑单线程"，无需显式锁即可保护共享状态。对应 Drogon 的"loop 亲和 + queueInLoop"：把对某状态的所有访问串行化到同一执行上下文。`bind_executor(strand, handler)` 把处理器绑定到 strand，是 Asio 的标准写法。
- **`shared_from_this` 生命周期延长**：Asio 教科书式连接对象（如经典 `tcp::connection : enable_shared_from_this`）在每个异步操作里捕获 `shared_from_this()`，保证"只要有在途异步操作，对象就存活"。这与本设计 1.8/1.9 推荐完全一致。
- **启示**：本仓库可把"对真正 loop 亲和资源的所有访问编排回该 loop（queueInLoop）"理解为 Asio 的 strand 串行化；把"回调捕获 `self`"理解为 Asio 连接对象的寿命延长。两者是同一思想在不同库的体现。（注意：本仓库的 `CacheMap` 已自带锁、**不**属于需要 strand/queueInLoop 串行化的资源；Asio 的真正对应物是回调对 `self` 的捕获。）

**Facebook folly**
- **`Future`/`SemiFuture` + Executor**：folly 用 future/continuation 链（`.thenValue`/`.thenError`）表达异步，执行点由 `Executor` 决定，避免裸回调嵌套。
- **`Executor::KeepAlive` token**：folly 用 `KeepAlive` 令牌保证"只要还有任务要在某 executor 上跑，该 executor 就不被销毁"——这是对"执行上下文生命周期"的显式管理，思想上对应本设计 1.3 用 `shared_ptr` 让"只要有使用者，存储就存活"。
- **启示**：folly 的经验支持本设计两点——(a) 用所有权令牌（`shared_ptr`/KeepAlive）显式表达生命周期依赖，而非隐式裸指针时序（针对 1.3）；(b) 用 future 链替代深层回调嵌套可降低生命周期错误概率（`TokenService::exchangeCodeForToken` 的多层嵌套是 1.9 的高风险点，未来可考虑用协程/future 重构，但属增强、非本次必需）。

**ScyllaDB Seastar**
- **`sharded<>` share-nothing per-core 模型**：seastar 每个 CPU 核一个 shard，状态分片到各核，**核间不共享指针**；跨核通信经显式消息传递（`submit_to(core, lambda)`）。这从架构上消除了数据竞争与跨线程生命周期问题。
- **future/continuation + 严格执行点**：所有异步经 future 链，continuation 在确定的 shard 上执行。
- **启示**：seastar 的 share-nothing 是"执行上下文亲和"的极致版本。本设计无需重构为 share-nothing；其原则可作一般性参考——**对真正的共享可变结构，要么把访问送回其所属上下文，要么改用本身线程安全的结构**。`submit_to(core, ...)` 与 Drogon `queueInLoop` 是同构原语。（注意：本仓库的 `CacheMap` **已是**线程安全结构，不属于需要 share-nothing 编排的情形。）

| 库 | 串行化/亲和原语 | 生命周期机制 | 对本设计的支撑 |
|---|---|---|---|
| Drogon/trantor | `runInLoop`/`queueInLoop`（用于**真正** loop 亲和资源；`CacheMap` 自带锁、无需编排） | `enable_shared_from_this`/`weak_from_this`、`runEvery`+`weak` | 1.8/1.9/1.10/1.11 回调捕获 `self`（含原 1.6 的 `CacheMap` 成员生命周期） |
| Boost.Asio | `strand` + `bind_executor` | 连接对象 `shared_from_this` | 回调寿命延长 |
| folly | Executor + future 链 | `KeepAlive` 令牌、`shared_ptr` | 1.3 显式所有权、降低嵌套（增强） |
| Seastar | `sharded<>` + `submit_to` | share-nothing（无跨核指针） | 一般性参考：对真正的共享可变结构不跨上下文共享 |

### 3) 本代码库推荐的统一模式（project-wide pattern）

综合上述，建议本仓库采用如下**一致的项目级模式**（均为设计建议，落地由 `tasks.md` 决定）：

1. **带异步回调的业务对象统一继承 `enable_shared_from_this`，回调捕获 `self`**：
   - `CachedOAuth2Storage`、`RedisOAuth2Storage`、`PostgresOAuth2Storage`、`TokenService`、`IdentityService`（以及 `ClientService`）继承 `std::enable_shared_from_this<T>`；异步 lambda 捕获 `auto self = shared_from_this();`（关键副作用必须完成 → `shared_ptr`）。
   - **接线前置**：`CachedOAuth2Storage`（及直连的 `RedisOAuth2Storage`/`PostgresOAuth2Storage`）必须经 `make_shared` 由**具体类型**创建（见 1.3/1.8）；服务类已是 `make_shared` 持有，无此约束。
   - **嵌套所有权（nested ownership）**：`PostgresOAuth2Storage`/`RedisOAuth2Storage` 在被 `CachedOAuth2Storage::impl_`（`unique_ptr<IOAuth2Storage>`）包裹时，其**自身**的 `shared_from_this()` 无效（会抛 `bad_weak_ptr`）。故**不要**让三个类都无条件调用各自的 `shared_from_this()`——那样会在缓存/包裹路径崩溃。统一择一并贯彻：**方案 A（首选）**由外层 `CachedOAuth2Storage` 捕获自身 `self`，传递性保活 `impl_`，内层无需自备 ESFT；或 **方案 B** 把 `impl_` 改为 `make_shared` 的 `shared_ptr<IOAuth2Storage>`，使内层 `shared_from_this()` 在包裹路径下也有效（详见 1.8）。
   - `OAuth2CleanupService` 已继承且 `start()` 已用 `weak_from_this()`；把 `runCleanup()` 对齐为同样的 `weak_from_this()`+`lock()`（可丢弃任务 → `weak_ptr`）。
   - 选择准则：**副作用必须完成 → 捕获 `shared_ptr`；任务可安全丢弃 → 捕获 `weak_ptr` 并 `lock()`。**

2. **`CacheMap` 无需 loop 编排（修订）**：
   - `drogon::CacheMap` 内部自带互斥锁、线程安全，**不需要** `queueInLoop` 把访问编排回某个 loop。保护它的方式与其它成员一致——让异步回调持有宿主对象的 `self`（`shared_ptr`），即同时保证 `tokenCache_/clientCache_` 这两个成员在回调期间存活。
   - **不得**为 `CacheMap` 引入 loop 编排：那会把 L1 命中的同步控制流改为异步，回归 Preservation 3.4/3.5。
   - 良性残留：look-aside 并发未命中会产生 N 次 DB 回源（stampede），属性能特征而非安全缺陷；如需可后续用单飞优化。

3. **`JwkManager` 的 init-once-then-read-only 契约**：
   - 文档化"仅在 `initAndStart` 期间 init 一次、此后只读"；理想态以 `std::shared_ptr<const JwkManager>` 不可变发布，类型层面禁止运行期变更（对应 folly 的"发布后只读"思想 + Asio 的"无共享可变"）。

4. **`storage_` 与各服务的所有权共享化**：
   - `OAuth2Plugin::storage_` 从 `unique_ptr` 改为 `shared_ptr<IOAuth2Storage>`，服务持 `shared_ptr`（或 `weak_ptr` 用时 `lock()`）。消除 1.3 的隐式裸指针时序约定（对应 folly KeepAlive：用所有权令牌表达"使用者在，资源就在"）。

5. **SIOF 易发的文件作用域全局 → 函数内静态或显式启动期初始化**：
   - `OAuth2StandardController` 的 `docs_` 自动注册改为 `initAndStart` 显式调用 `initApiDocs()`（1.1）。
   - `RequestValidationFilter::OAUTH2_VALIDATION_RULES` 改为函数内静态访问器（Meyers Singleton），合并构造与一次性填充（1.2）。
   - `AuthorizationFilter::loadConfig()` 用 `std::call_once` 或移到启动期（1.4）。

> 这套模式的统一性收益：所有异步入口遵循同一"`self` 捕获 + loop 编排"规则，新代码可据此 review；与 Drogon 原生惯用法、Asio/folly/seastar 的成熟实践一致，降低后续维护与扩展时再次引入并发/生命周期缺陷的概率。

## Testing Strategy

> 验证策略为**高层规划**，本阶段不实现测试代码。整体遵循两阶段：先在**未修复代码（F）**上让 sanitizer / 竞态测试**复现缺陷（surface counterexamples）**以确认根因；修复后（F'）再验证缺陷消除且回归不变。属性对应「## Correctness Properties」的 Property 1–4。

### Validation Approach

- **复现优先**：先在 F 上用 ThreadSanitizer (TSan) / AddressSanitizer (ASan) + 针对性竞态/关停测试触发 C(X)，得到具体反例（TSan 数据竞争报告、ASan heap-use-after-free 栈）。若无法复现，则根因假设被否证（refute），需回到「## Hypothesized Root Cause」重新假设。
- **此规则的一个重要推论（与 1.6 的修订一致）**：正因为"无法复现即否证根因"，所以**不能**再期望对 `CacheMap` 出现 TSan 数据竞争——`CacheMap` 自带锁，并发访问**不会**触发 race，若仍把"CacheMap 跨 loop 访问 = 数据竞争"列为预期反例，该预期将**自我否证**（测试跑不出 race ⇒ 按规则反推根因被否证）。因此 1.6 不再作为 B 类竞争用例，改由 **C 类 UAF 用例**（对象先于回调析构）覆盖其真实问题。
- **修复后验证**：在 F' 上重跑同一组测试，确认 C(X) 下缺陷消除（Property 1–3），并跑保持不变套件确认 ¬C(X) 行为一致（Property 4）。

### Exploratory Bug Condition Checking（在未修复代码上复现）

**Goal**：在实现修复前，先用反例证明缺陷存在并确认/否证根因。

**Test Plan / Test Cases**：
1. **A 类（init order）**：用不同链接/初始化顺序构建，检查 OpenApi 文档端点数与校验规则集是否完整一致（1.1/1.2）。在某些链接顺序下可能观察到端点缺失/规则空——作为 SIOF 反例（可能依平台显现不稳定）。
2. **B 类（数据竞争）**：在 TSan 下并发首次命中 `AuthorizationFilter::doFilter`（1.4）、并发 `JwkManager` 读写时序（1.5）——预期 TSan 报告 data race（will fail on unfixed code）。**注意：不再把 `CacheMap` 跨 loop 访问列为 B 类用例**——`CacheMap` 线程安全，并发访问不会产生 race（强行预期 race 会按"无法复现即否证"规则自我否证）。`OAuth2Metrics`（1.7）当前无共享计数，亦无 race 可复现，仅作为"引入共享计数后须原子"的护栏说明。
3. **C 类（UAF）**：构造"对象销毁先于在途异步回调"的关停竞态：发起 `CachedOAuth2Storage` 异步读/写后立即 `storage_.reset()`（**1.8，含原 1.6 的 `tokenCache_/clientCache_` 成员**）；在 redis 模式 / postgres 无缓存路径下对直连的 `RedisOAuth2Storage`/`PostgresOAuth2Storage` 做同样的关停竞态（1.8 扩展）；销毁 `TokenService`/`IdentityService` 后让链回调到达（1.9）；`OAuth2CleanupService` 在 Redis 命令派发与回复间销毁（1.10）——预期 ASan 报告 heap-use-after-free（will fail on unfixed code）。

**Expected Counterexamples**：TSan 的 read/write data-race 栈（仅 1.4/1.5）；ASan 的 use-after-free 栈（1.8 含原 1.6、1.9、1.10）；以及 SIOF 下文档/规则不完整的快照差异（1.1/1.2）。**`CacheMap` 本身不应出现 race 反例**——其问题在 ASan（成员随宿主析构的 UAF）而非 TSan。

### Fix Checking（修复后，对 C(X) 输入）

**Goal**：对所有触发缺陷条件的输入，F' 满足期望行为。

**Pseudocode:**
```
FOR ALL input WHERE isBugCondition_A(input) OR isBugCondition_B(input) OR isBugCondition_C(input) DO
  result := fixed(input)
  ASSERT initOrderSafe(result)        // A: 文档/规则完整且顺序无关 (Property 1)
     AND noDataRace(result)           // B: TSan 无 warning      (Property 2)
     AND no_use_after_free(result)    // C: ASan 无 UAF          (Property 3)
     AND (callbackRunsSafely(result) OR callbackSkippedSafely(result))
END FOR
```
对应同一组复现测试在 F' 上重跑，断言 sanitizer 干净、文档/规则完整。

### Preservation Checking（修复后，对 ¬C(X) 输入）

**Goal**：对所有不触发缺陷的正常路径，F' 与 F 结果一致。

**Pseudocode:**
```
FOR ALL input WHERE NOT isBugCondition(input) DO
  ASSERT original(input) = fixed(input)
END FOR
```

**Testing Approach**：推荐**属性测试（property-based testing）**做保持不变验证——自动生成大量正常路径输入（随机 client/scope/token/grant 组合、随机请求路径、缓存命中/未命中序列），对比 F 与 F' 的可观测输出。先在 F 上记录基线行为，再断言 F' 与基线逐位一致。

**Test Cases**：
1. **文档/规则快照保持**：在正常顺序下，OpenApi 文档与 `getValidationRules(path)` 结果与基线一致（3.1/3.7）。
2. **核心流程保持**：授权码、令牌签发/刷新/校验/内省/吊销在正常时序下响应/错误码/JSON 与基线一致（3.2）。
3. **JWKS/id_token 保持**：相同密钥/`kid`/RS256 下签发结果一致（3.3）。
4. **缓存语义保持**：L1/L2 命中、回源、失效、TTL 行为与基线一致（3.4）。
5. **清理锁语义保持**：有锁/无锁两场景的清理行为不变（3.6）。

### Unit Tests
- `RequestValidationFilter::getValidationRules` 对各 path 的规则返回（函数内静态化后）与基线一致。
- `AuthorizationFilter::checkAccess` 在加载完成后对各 path/role 的放行/拒绝判定不变。
- `JwkManager` init-once 守卫：重复 `init()` no-op；`signJwt`/`getJwks` 在 init 后返回稳定结果。
- `OAuth2CleanupService::runCleanup` 在 `weakSelf.lock()` 失败时安全跳过（注入已过期 weak_ptr）。

### Property-Based Tests
- 生成随机并发访问序列，验证 `AuthorizationFilter` 一次性初始化在 TSan 下无竞争（Property 2）。
- 生成随机"对象销毁 / 回调到达"的交错时序，验证 `CachedOAuth2Storage`（含 `tokenCache_/clientCache_` 成员）/直连 `RedisOAuth2Storage`/`PostgresOAuth2Storage`/`TokenService`/`IdentityService`/`OAuth2CleanupService` 在 ASan 下无 UAF（Property 3）。
- 生成随机正常请求/缓存序列，断言 F' 与 F 输出等价（Property 4 保持不变）。注：look-aside 并发未命中导致的 N 次回源（stampede）属允许的非线性一致行为，断言应比较**最终收敛值**而非要求严格线性一致。

### Integration Tests
- 端到端 OAuth2 流程（authorize → token → refresh → introspect → revoke）在 TSan/ASan 构建下跑通且 sanitizer 干净。
- 关停竞态集成测试：高并发请求进行中触发插件 `shutdown()`，断言无崩溃、无 UAF，在途请求安全完成或安全失败；并覆盖 redis 模式与 postgres 无缓存路径下直连存储的关停竞态。
- 缓存并发集成测试：并发读写命中/回源，确认 L1/L2 命中率与返回值与基线一致（`CacheMap` 跨线程访问本身安全，**不**期望也**不需要** loop 编排）。

> 工具与命令（高层，不在本阶段执行）：以 `-fsanitize=thread`、`-fsanitize=address` 分别构建测试目标运行上述套件；CI 中将 TSan/ASan 干净作为合入门禁。
