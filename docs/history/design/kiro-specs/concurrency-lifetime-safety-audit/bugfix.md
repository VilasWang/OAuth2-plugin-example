# Bugfix 需求文档（并发与生命周期安全审计）

## Introduction

本文档针对 Drogon 框架下的 OAuth2 插件 / 服务端代码库（`OAuth2Plugin/` 与 `OAuth2Server/`）做一次系统性的**并发与对象生命周期安全审计**，并以"缺陷条件（Bug Condition）"方法整理出每一类缺陷的**触发条件 C(X)**、**期望行为 P(result)** 和**必须保持不变的行为（回归防护）**。

> 重要范围约束：本文档及本规格的目标是**分析与修复计划**，**不在本阶段修改任何源代码**。`bugfix.md` 只负责描述缺陷、缺陷条件与期望行为；具体的修复方案、Drogon / Boost.Asio / folly / seastar 等成熟库的对照实现，以及落地步骤将放入下一阶段的 `design.md` 与 `tasks.md`。

Drogon 默认以**多个事件循环 IO 线程**运行，HTTP 请求被分发到不同的 `EventLoop`；数据库（`DbClient`）、Redis（`RedisClient`）等客户端各自绑定在某个 loop 上，其异步回调在对应 loop 线程上执行。`Controller` 与 `Filter` 在 Drogon 中是**进程级单例**。这三点是本审计中绝大多数缺陷的根本背景。

审计覆盖三大缺陷类别：

- **类别 A —— 静态/全局变量的初始化顺序与相互依赖**（Static Initialization Order Fiasco 及跨翻译单元依赖）。
- **类别 B —— 多线程线程安全**（共享可变状态的数据竞争、loop 绑定结构跨 loop 访问）。
- **类别 C —— 异步回调的对象生命周期安全**（回调捕获裸 `this`/裸指针，在对象销毁后回调仍在途 → 悬垂访问 / use-after-free）。

下文每条缺陷都标注了在代码中可观测到的具体位置，作为缺陷条件的起点；审计结论比种子样例更广。

---

## Bug Analysis

> 说明：以下采用缺陷条件方法。**F** 表示修复前的现状函数，**F'** 表示修复后的函数；**C(X)** 为触发缺陷的输入/时序条件；**¬C(X)** 为不触发缺陷、必须保持原状的输入/时序。

### Current Behavior (Defect)

本节描述在现状代码（F）下可观测到的错误行为。按类别分组，但子句在本节内统一连续编号（1.x）。

#### 类别 A：静态/全局初始化顺序与相互依赖

1.1 WHEN 进程在 `main()` 之前执行静态初始化时 THEN 系统在 `OAuth2Plugin/src/controllers/OAuth2StandardController.cc` 中构造文件作用域全局对象 `OAuth2StandardControllerDocs docs_`，其构造函数调用 `OAuth2StandardController::initApiDocs()`，进而写入位于另一个翻译单元的全局 OpenApi 注册表（`OpenApiGenerator` 的函数内静态 `endpoints`/`apiInfo`/`initialized`/`serverConfig`），两个全局对象的构造次序在跨翻译单元间未定义，存在初始化顺序依赖（SIOF）。

1.2 WHEN 进程加载 `RequestValidationFilter` 翻译单元时 THEN 系统构造命名空间作用域的非平凡全局对象 `RequestValidationFilter::OAUTH2_VALIDATION_RULES`（`std::map`），其初始化顺序相对其他全局对象未定义，且该 map 的填充依赖运行期 `std::call_once`，初始化时机与构造时机分离。

1.3 WHEN `OAuth2Plugin::initAndStart()` 执行初始化时 THEN 系统按 `storage_ → tokenService_/clientService_/identityService_ → cleanupService_` 的顺序构造，后三者保存的是 `storage_.get()` 的**裸指针**（`IOAuth2Storage*`），其正确性完全依赖"`storage_` 必须先于、且销毁后于这些服务"这一隐式时序约束，但该约束没有在类型系统或代码注释中被保证。

#### 类别 B：多线程线程安全

1.4 WHEN 多个 IO 线程在启动后并发处理第一批请求、首次进入 `OAuth2Plugin/src/filters/AuthorizationFilter.cc` 的 `doFilter()` → `loadConfig()` 时 THEN 系统在无任何互斥/原子保护的情况下对单例成员 `rules_`、`publicPaths_`、`initialized_` 做"检查 `initialized_` 再写入"的 check-then-act，多个线程可同时通过检查并并发写同一组容器，产生数据竞争（可能崩溃或规则部分加载）。

1.5 WHEN `JwkManager::init()` 写入 `rsaKey_`/`initialized_`/`kid_` 的同时，请求处理线程并发调用 `signJwt()` / `getJwks()` 读取这些成员时 THEN 系统在无同步原语的情况下发生读写竞争；当前依赖"启动期初始化、运行期只读"的 happens-before 假设，但该假设既未文档化也未由代码强制（`setJwkManager()` 等非 const 变更入口未被约束）。

1.6 WHEN `CachedOAuth2Storage` 的成员 `tokenCache_`/`clientCache_`（`drogon::CacheMap`，构造时绑定 `drogon::app().getLoop()`）被来自 Redis/DB 客户端回调（运行在其各自所属 loop 线程上）调用 `insert()`/`erase()`/`findAndFetch()` 时 THEN 系统从非其绑定 loop 的线程访问了 loop 绑定的数据结构，构成跨 loop 的并发访问。

1.7 WHEN 高并发请求同时经过可观测性打点 `OAuth2Plugin/src/observability/OAuth2Metrics.cc` 时 THEN 系统当前仅做 `LOG_INFO` 输出（无共享计数器，暂无竞争），但文件内注释自承"假设松一致性（loose consistency）"，一旦在此处引入真实的共享计数器即会成为无同步的竞争点（潜在缺陷）。

#### 类别 C：异步回调的对象生命周期安全

1.8 WHEN `CachedOAuth2Storage` 的 `getAccessToken()`/`saveAccessToken()`/`revokeAccessToken()` 把捕获**裸 `this`** 的 lambda 交给 `redisClient_->execCommandAsync(...)` 或 `impl_->...` 异步操作，而此后 `OAuth2Plugin::shutdown()` 调用 `storage_.reset()` 销毁了该 `CachedOAuth2Storage` 对象时 THEN 在途回调仍通过悬垂的 `this` 访问已析构的 `tokenCache_`/`clientCache_`/`redisClient_`，产生 use-after-free（该类未继承 `enable_shared_from_this`）。

1.9 WHEN `TokenService`/`IdentityService` 把捕获裸 `this` 的 lambda 交给 `storage_->...` 异步链（如 `exchangeCodeForToken` 的多层嵌套、`refreshAccessToken`、`ensureSubjectMapping`、`handleFirstTimeLogin`），而所属服务对象在异步完成前被销毁时 THEN 在途回调通过悬垂 `this` 访问 `storage_`/`accessTokenTtl_`/`jwkManager_` 等成员，产生 use-after-free（两类均未继承 `enable_shared_from_this`）。

1.10 WHEN `OAuth2CleanupService::runCleanup()` 把捕获裸 `this` 的 lambda 交给 `redis->execCommandAsync(...)`，而服务对象在 Redis 命令派发与其回复之间被销毁时 THEN 回调通过悬垂 `this` 调用 `storage_->deleteExpiredData()`，产生 use-after-free；这与同文件 `start()` 中 `runEvery` 已正确使用 `weak_from_this()` 的做法不一致。

1.11 WHEN `OAuth2StandardController` 的授权链（`authorize` → `validateRedirectUri` → `validateClientScopes` → `validateUserRolesForScopes` → `getInternalUserId` → …）逐层把捕获裸 `this` 的 lambda 透传到底层异步存储调用时 THEN 控制器单例的裸 `this` 被长链异步持有；虽然控制器为进程级长生命周期、实际崩溃风险较低，但其生命周期成为隐式风险面，缺乏显式的生命周期保障。

### Expected Behavior (Correct)

本节描述修复后（F'）应当观测到的正确行为，与上节一一对应。

#### 类别 A

2.1 WHEN 进程执行静态初始化时 THEN 系统 SHALL 保证 OpenApi 文档注册不依赖跨翻译单元的全局对象构造次序（例如改为显式的、在 `main()`/插件初始化阶段触发的注册，或使用首次访问即初始化的函数内静态），使得无论翻译单元初始化顺序如何，注册结果都正确且完整。

2.2 WHEN 进程加载校验规则时 THEN 系统 SHALL 以确定的、无顺序依赖的方式提供 `OAUTH2_VALIDATION_RULES`（保持现有 `call_once` 一次性初始化语义），并保证在任何全局初始化顺序下读取到完整规则集。

2.3 WHEN `OAuth2Plugin` 初始化与销毁时 THEN 系统 SHALL 使 `storage_` 与依赖它的各服务之间的生命周期关系显式且被保证（storage 先构造、后析构，或服务以可被验证的方式共享 storage 的所有权），不再依赖隐式的裸指针时序约定。

#### 类别 B

2.4 WHEN 多个 IO 线程并发首次进入 `AuthorizationFilter::doFilter()` 时 THEN 系统 SHALL 以线程安全的一次性初始化（如 `std::call_once` 或在 `initAndStart`/过滤器构造期完成加载）保证 `rules_`/`publicPaths_` 只被安全地初始化一次，且并发读取不产生数据竞争。

2.5 WHEN 请求线程读取 `JwkManager` 的签名密钥状态时 THEN 系统 SHALL 通过明确的 happens-before 保证（启动期完成初始化、运行期只读，并以文档/断言/不可变化设计加以约束）确保 `signJwt()`/`getJwks()` 读取到一致且完全初始化的密钥状态，无读写竞争。

2.6 WHEN `CachedOAuth2Storage` 的缓存在异步回调中被访问时 THEN 系统 SHALL 保证对 `tokenCache_`/`clientCache_` 的访问发生在其绑定的 loop 上（或采用本身线程安全的缓存机制），消除跨 loop 并发访问。

2.7 WHEN 在 `OAuth2Metrics` 中维护任何共享计数时 THEN 系统 SHALL 使用线程安全手段（原子量或专用线程安全的指标库），保证并发打点不产生数据竞争或计数丢失。

#### 类别 C

2.8 WHEN `CachedOAuth2Storage` 的异步回调在对象可能被销毁的时序下完成时 THEN 系统 SHALL 保证回调要么安全持有对象（共享所有权/`shared_from_this` 语义），要么在对象已失效时安全跳过，绝不通过悬垂 `this` 访问 `tokenCache_`/`clientCache_`/`redisClient_`。

2.9 WHEN `TokenService`/`IdentityService` 的异步链在服务对象可能被销毁的时序下完成时 THEN 系统 SHALL 保证回调对 `this` 及其成员的访问是生命周期安全的（共享所有权或等价机制），不产生 use-after-free。

2.10 WHEN `OAuth2CleanupService::runCleanup()` 的 Redis 回调在服务对象可能被销毁的时序下完成时 THEN 系统 SHALL 与 `runEvery` 保持一致地使用 `weak_from_this()`（或等价机制），在对象已销毁时安全跳过，绝不通过悬垂 `this` 访问 `storage_`。

2.11 WHEN `OAuth2StandardController` 的多层异步链在执行中时 THEN 系统 SHALL 以显式且一致的生命周期策略（共享所有权或框架保证的单例长生命周期约定，并明确文档化）持有回调上下文，使裸 `this` 不再成为隐式风险面。

### Unchanged Behavior (Regression Prevention)

本节描述修复后必须保持不变的行为（对应 ¬C(X)，即不触发上述缺陷的正常路径），用于回归防护。

3.1 WHEN 单线程/正常启动顺序下加载 OpenApi 文档与校验规则时 THEN 系统 SHALL CONTINUE TO 暴露与现状一致的 API 文档内容与请求校验规则集（端点、参数、规则不变）。

3.2 WHEN OAuth2 授权码、令牌签发、刷新、校验、内省、吊销等核心流程在正常（非销毁期、单次初始化已完成）时序下执行时 THEN 系统 SHALL CONTINUE TO 返回与现状完全相同的业务结果（相同的成功响应、错误码与 JSON 结构）。

3.3 WHEN 初始化完成后、运行期对 `JwkManager` 只读访问时 THEN 系统 SHALL CONTINUE TO 使用相同的签名密钥、`kid` 与 RS256 算法签发与现状一致的 id_token / JWKS。

3.4 WHEN 缓存命中（L1/L2）或未命中并回源 DB 的正常读写路径执行时 THEN 系统 SHALL CONTINUE TO 维持与现状相同的缓存语义（写穿、回源、失效、TTL 行为不变）。

3.5 WHEN 在对象生命周期内（未发生销毁）异步回调正常完成时 THEN 系统 SHALL CONTINUE TO 以与现状相同的方式调用业务回调并产生相同的副作用（令牌入库、缓存写入、审计日志、清理任务执行等）。

3.6 WHEN `OAuth2CleanupService` 在分布式锁可用/不可用两种场景下到期触发清理时 THEN 系统 SHALL CONTINUE TO 维持现有的"获得锁才清理 / 无锁单实例清理"的行为语义。

3.7 WHEN `AuthorizationFilter` 在初始化完成后对已认证/未认证请求做 RBAC 与 public path 判定时 THEN 系统 SHALL CONTINUE TO 返回与现状一致的放行 / 401 / 403 结果。

---

## 缺陷条件（Bug Condition）形式化摘要

> 下列伪代码用于把上述子句提炼为可验证的缺陷条件 C(X) 与不变式（preservation）。**F** 为现状、**F'** 为修复后。

### 类别 A —— 初始化顺序与依赖

```pascal
FUNCTION isBugCondition_A(X)
  INPUT: X = 静态初始化序列 / 插件初始化序列
  OUTPUT: boolean
  // 当某全局/服务在其依赖项尚未（或已不再）就绪时被构造或使用
  RETURN dependsOnAnotherGlobalCtorOrder(X)
      OR usesRawStoragePointerAcrossLifetime(X)
END FUNCTION

// Preservation：正常顺序下结果不变
FOR ALL X WHERE NOT isBugCondition_A(X) DO
  ASSERT F(X) = F'(X)   // 文档内容、校验规则、业务结果一致
END FOR
```

### 类别 B —— 线程安全

```pascal
FUNCTION isBugCondition_B(X)
  INPUT: X = (并发访问时序, 共享可变状态 S, 访问所在线程/loop)
  OUTPUT: boolean
  RETURN concurrentWriteAndReadWithoutSync(X, S)        // 1.4 / 1.5 / 1.7
      OR loopBoundStructureAccessedFromForeignLoop(X)   // 1.6
END FUNCTION

// Property（Fix Checking）：对所有触发并发竞争的输入，修复后无数据竞争且结果一致
FOR ALL X WHERE isBugCondition_B(X) DO
  result ← F'(X)
  ASSERT noDataRace(result) AND resultConsistentWithSequential(result)
END FOR

// Preservation
FOR ALL X WHERE NOT isBugCondition_B(X) DO
  ASSERT F(X) = F'(X)
END FOR
```

### 类别 C —— 异步回调生命周期

```pascal
FUNCTION isBugCondition_C(X)
  INPUT: X = (异步操作 op, 捕获方式 capture, 对象在回调完成前是否可能被销毁)
  OUTPUT: boolean
  RETURN capturesRawThisOrRawPtr(capture)
     AND objectMayBeDestroyedBeforeCallback(op, X)
END FUNCTION

// Property（Fix Checking）：对所有"对象先于回调销毁"的时序，修复后不得 UAF
FOR ALL X WHERE isBugCondition_C(X) DO
  result ← F'(X)
  ASSERT no_use_after_free(result)
     AND (callbackRunsSafely(result) OR callbackSkippedSafely(result))
END FOR

// Preservation：对象存活期间，行为与现状一致
FOR ALL X WHERE NOT isBugCondition_C(X) DO
  ASSERT F(X) = F'(X)
END FOR
```
