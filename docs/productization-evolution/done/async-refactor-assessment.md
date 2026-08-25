# 异步回调地狱改造方案对比评估报告

> **范围**：本报告为**评估文档**，不含可编译代码，不改动任何源码 / 构建配置 / 规范。所有伪代码仅用于形态对照。
> **决策前提**（来自需求方）：
> 1. 先出对比评估报告，暂不动手；
> 2. 未来若动手，先以 **1 个文件（`GitHubController.cc`）作为示范**；
> 3. **协程先不引入**（C++20 `co_await` / `CoroMapper` 本轮排除）。
>
> 因此本报告重心为 **C++17 下可行方案对比**，协程方案（方案 C）仅作"被排除的长期演进参考"脚注。

---

## 1. 背景与现状速览

### 1.1 异步模型：纯回调、零抽象、C++17 锁定

| 维度 | 现状 |
|------|------|
| C++ 标准 | **C++17**（`CMAKE_CXX_STANDARD 17`，`EXTENSIONS OFF`） |
| 异步机制 | **唯一的机制是裸回调** `std::function<void(...)>`；全仓库 `co_await` / `<coroutine>` / `std::future` / `std::promise` / `std::async` **零命中** |
| 异步库 | 无 Boost.Asio / cppcoro / folly；底层异步原语完全来自 **Drogon 1.9.13**（`Mapper::findBy` / `execSqlAsync` / `HttpClient::sendRequest`，全部 `(successCb, errorCb)` 双回调签名） |
| 抽象层 | **无 Future / Promise / Task / Continuation 类型**；只有按业务命名的回调别名（`IOAuthHttpClient::ResultCallback`、`ISocialAccountRepository::LookupCallback` 等），本质都是 `std::function<void(Result)>` |
| 生命周期约定 | `std::make_shared<std::function<...>>(std::move(cb))`，变量名 `sharedCb`（identity 层）或 `callbackPtr`（drogon 层）—— 这是项目**唯一**的异步"工具" |

### 1.2 痛点量化（最具代表性的两个文件）

**`libs/drogon/src/controllers/GitHubController.cc::login`**

- 方法体 **L95–L659，共 564 行**。
- fallback 路径嵌套链：`sendRequest` → `sendRequest` → `findBy(SubjectMappings)` → `findBy(Users)` / `execSqlAsync(INSERT users)` → `insert(SubjectMappings)` → `insert(UserRoles)` → `insert(AccessToken)` → `insert(RefreshToken)` → 响应。**7 层 lambda 嵌套**，最深缩进约 **12–13 层（24–26 个前导空格）**。
- **`issueTokens` 存在两份几乎逐字重复的副本**：副本 A（`GitHubController.cc:154`，WITH_SOCIAL 路径）与副本 B（`GitHubController.cc:382`，fallback 路径内嵌），仅 `int64_t` vs `int` 签名差异。
- fallback 路径内 **9 处 `DrogonDbException` 错误回调**，每处重复 `respondError(req, callbackPtr, "DB_QUERY_ERROR", "<ctx>: " + e.base().what())` 模板；外加 3 层 `try/catch` 共 6 个 catch 块。错误处理代码量约占方法 1/3。

**`libs/drogon/src/admin/UserAdminService.cc`**

- 全文件 **35 个 Mapper / execSqlAsync 调用点**（回调密度最高）。
- `listUsers`（`UserAdminService.cc:74`）为替代 JOIN（TECH_SPECS §二禁用 JOIN），用 3 个串行异步查询互相嵌套：`findBy(Users)` → `findBy(UserRoles, IN)` → `findBy(Roles, IN)`，每层一对成功/错误回调。
- 团队已用 `fetchUserRoleNames`（`UserAdminService.cc:192`）做了部分抽离，但它本身仍是 2 层嵌套，且 `listUsers` 因 shape 不同未复用它 —— **抽离不彻底，重复依然存在**。

### 1.3 规范层证据（`TECH_SPECS.md` 原文）

§一 异步编程规范（原话节选）：

| 接口类型 | 优先级 | 说明 |
|----------|--------|------|
| 异步回调 | **[+] 最高** | `Mapper::findOne`, `execSqlAsync` |
| 同步接口 | **[!] 限制** | `Mapper::findBy` with future（非必要禁止） |
| 协程接口 | **[-] 禁止** | `CoroMapper`（严格禁止使用） |

> Lambda 捕获规范：`[+]` 捕获 `sharedCb`；`[-]` 捕获裸指针 `[this]`, `[&var]`。

**关键观察（规范与现实脱节）**：

1. `CoroMapper` 被显式标 `[-] 禁止（严格禁止使用）`，直接堵死 Drogon 原生 coroutine ORM 路径（详见方案 C）。
2. 规范把"异步回调"列为最高优先级，等于在文档层**背书**回调地狱写法。
3. `[this]` 被规范禁用，但 `GitHubController.cc:154,277,321,374` 的 lambda 大量按值捕获 `this`（`[this, callbackPtr, req]`）—— **规范在执行层已被违反**，说明现有约束既不现实也未被遵守。

---

## 2. 业界怎么处理回调地狱（共识速览）

不管语言如何，回调地狱的解法是同一条演进路线，按抽象层次由低到高：

### 2.1 Future / Promise + 链式 `.then()`
把"回调里再开异步"拍平成一条链。代表实现：

- **Java** `CompletableFuture.thenCompose(...)` / `whenComplete(...)`
- **谷歌** `com.google.common.util.concurrent.ListenableFuture` + `Futures.addCallback` / `Futures.transformAsync`（Google Java 核心异步抽象）
- **C++** `std::future<T>::then`（C++23 TS 形态，C++17 下需自研或用第三方）；`folly::Future`（Meta，支持 `.then()` / `.via(executor)` / `SemiFuture`）
- **Rust** `Future` + `.await`（底层即此模型）

### 2.2 async / await / Coroutine
目前所有"优秀代码库"的主流答案。把链式调用再写成**同步形态**，彻底消除缩进：

- **C++20** `co_await` / `drogon::Task<T>` / `CoroMapper<T>`（Drogon 原生支持）
- **C# / JS / Python / Kotlin** 的 `async/await`
- **Go** 用 goroutine + channel 从根上回避此问题
- **Rust** `async fn` + `.await`

### 2.3 ReactiveX / Observable
`Observable<T>` + `flatMap` / `concatMap`，能表达"流"，但学习曲线陡，对单值异步链是杀鸡用牛刀。谷歌内部用 Rx 风格较少。

### 2.4 权威主张一句话

| 来源 | 主张 |
|------|------|
| Google C++ Style Tips of the Week（#130/#155/#177） | 推荐 `.then()` 链式替代裸回调链；coroutine 是更长远的方向 |
| Google SRE / Go 团队 | goroutine + channel 从根上回避 |
| folly 文档（Meta） | "Avoid callback hell: use `.then()` and `via(executor)`"，已全面转向 `folly::coro::Task` |
| C++ Core Guidelines（Stroustrup/Sutter） | 避免裸回调链，优先 coroutine |
| Drogon 官方 / 作者 an-tao | 推荐 `CoroMapper<T>` + `co_await`；纯回调是 legacy 风格 |

**一句话共识**：用 coroutine 或 Future/Promise 把回调链拍平；**业界已基本不用"裸回调 + sharedCb 穿透"这种 10 年前的写法**。本项目当前正处在这个被淘汰的形态上，且连 `std::future` 都未使用。

---

## 3. 方案对比矩阵（核心）

三方案并列。每栏含：理念、接口形态、GitHubController 伪代码、工作量、风险、回归点、可逆性。

### 方案 A —— 纯架构重构（C++17，零新依赖，零规范改动）

**理念**：不动异步模型，通过"抽函数 + 显式状态对象 + 统一错误处理"把多层嵌套拆平。

**手段**：
1. 把 `issueTokens` 的两份副本抽成**成员方法**（消除重复）。
2. 统一错误回调 helper：`respondDbError(req, callbackPtr, ctx, e)`（消除 9 处重复模板）。
3. 把 fallback 路径的 7 层切成**具名方法 + Flow 状态对象**（`GitHubLoginFlow`，持有 `req/callbackPtr/db/provider/subject/githubLogin/githubEmail`），每一步是一个命名方法：`exchangeToken()` → `fetchUserinfo()` → `resolveMapping()` → `ensureUser()` → `issueTokens()`。
4. 复用 `fetchUserRoleNames` 已有模式，扩展到 social 流程的共用子链。

**伪代码形态**（示意，非可编译）：
```cpp
// 重构后：login 方法体变成线性流程编排
void GitHubController::login(req, callback) {
    auto flow = std::make_shared<GitHubLoginFlow>(req, std::move(callback), dbClient());
    flow->start();   // 内部串行调用各步骤，每步是一个具名方法
}

// GitHubLoginFlow::start → exchangeToken → fetchUserinfo → resolveMapping ...
// 每个方法是单层回调，状态在 flow 对象成员里，不再层层 lambda 捕获
void GitHubLoginFlow::exchangeToken() {
    client_->sendRequest(tokenReq,
        [self = shared_from_this()](ReqResult r, HttpResponsePtr resp) {
            if (failed(r, resp)) return self->fail("NET_CONNECTION_FAILED", ...);
            self->accessToken_ = parseToken(resp);
            self->fetchUserinfo();
        });
}
```

| 维度 | 评估 |
|------|------|
| 工作量 | 中。GitHubController 单文件约 -200 行净减（去重 + 去重复错误回调），但需新建 Flow 类（~150 行） |
| 风险 | **低**。不引入新抽象，行为等价重构；可逐方法迁移 + 单测 |
| 回归点 | Flow 对象生命周期（`shared_from_this`）、状态字段初始化顺序、`issueTokens` 两份副本合并时签名差异（`int64_t` vs `int`） |
| 可逆性 | **高**。纯重排，git 历史可读 |
| 结论 | **治标**：缩进仍在（每步仍是单层回调），但可读性、重复、错误处理一致性显著改善 |

### 方案 B —— Future/Promise 链式抽象（C++17，自研轻量 Task，需改接口）

**理念**：在 `libs/common` 引入一个轻量 `Future<T>` + `.then()` + 错误传播（参考 `folly::Future` 精简版），把现有 `&&callback` 接口改成返回 `Future<T>`，把回调链拍平成线性链。

**手段**：
1. `libs/common` 新增 `Future<T>` / `Promise<T>`（约 150 行，含 executor 归属 + 异常传播）。
2. 接口签名破坏性改动：`IUserRepository` / `ISocialAccountRepository` / `IOAuthHttpClient` 的 `void m(args, &&callback)` 改为 `Future<T> m(args)`。
3. Adapter 层（`DrogonOAuthHttpClient`）用 `Promise` 把 Drogon 的 `(successCb, errorCb)` 回调包成 `Future`。
4. Controller / Service 把嵌套回调改写为 `.then()` 链。

**伪代码形态**（示意，非可编译）：
```cpp
// 重构后：GitHubController fallback 用 .then() 拍平
void GitHubController::login(req, callback) {
    exchangeToken(req)                              // Future<OAuthHttpResult>
        .then([](auto tok){ return fetchUserinfo(tok.accessToken); })   // Future<Userinfo>
        .then([](auto u){ return resolveMapping(u); })                  // Future<Mapping>
        .then([](auto m){ return ensureUser(m); })                      // Future<User>
        .then([](auto u){ return issueTokens(u); })                     // Future<Tokens>
        .then([callback](auto t){ callback(jsonResponse(t)); })
        .onError([callback, req](auto e){ respondError(req, callback, e); });
}
```

| 维度 | 评估 |
|------|------|
| 工作量 | 大。~150 行库 + 4 个接口（`IUserRepository` 9 方法 / `ISocialAccountRepository` 2 方法 / `IOAuthHttpClient` 2 方法 / `TokenService` 7 方法）签名改 + 6–8 个实现类 + 测试迁移 |
| 风险 | **高**。自研 Future 的线程安全 / executor 归属 / 异常传播是 C++17 下出了名易错的点；接口破坏性改动波及所有实现类与 mock 测试 |
| 回归点 | 自定义 Future 的 move 语义、跨线程续体调度、`std::exception_ptr` 传播、Drogon 事件循环线程归属 |
| 可逆性 | 中。一旦接口改了，回退成本高 |
| 结论 | **治本**：线性链彻底消除缩进；但 C++17 自研 Future 心智成本高，**业界已较少走此路**（要么升 C++20 用 coroutine，要么不引入新模型） |

> 注：方案 B 若引入 `folly` 或 `boost::future` 可省去自研成本，但本项目的依赖极简（conanfile 仅 drogon/openssl/jsoncpp/hiredis/libcurl/brotli/zlib/libcbor/gtest），引入 folly 重量级依赖与项目风格不符，故按"自研轻量"评估。

### 方案 C —— C++20 coroutine + Drogon CoroMapper（本轮排除，长期演进参考）

**排除理由**（按需求方决策：协程先不引入）：需 (1) 解禁 `TECH_SPECS.md` §一 CoroMapper 禁令；(2) 12 份 CMakePresets 的 `compiler.cppstd=17` 全改 `20`；(3) conan profile detect 默认值覆盖。

**记录：工具链无阻力**。CI 三平台默认工具链均原生支持 `<coroutine>`：Linux ubuntu-22.04 GCC 11、Windows VS 17 2022、macOS Apple Clang。阻力纯属**规范级 + 配置级**，非技术不可行。

**若未来解禁，GitHubController 会变成什么样**（对照伪代码，仅作长期参考）：
```cpp
drogon::Task<HttpResponsePtr> GitHubController::login(req) {
    try {
        auto tok   = co_await exchangeToken(req);
        auto info  = co_await fetchUserinfo(tok);
        auto user  = co_await ensureUser(info);
        co_return co_await issueTokens(user);
    } catch (const std::exception &e) {
        co_return errorResponse(req, e);
    }
}
```
7 层嵌套塌成线性 4 行。**这是 Drogon 官方推荐、谷歌/folly 现代方向，也是本项目终极形态**——但本轮不做。

### 三方案对比总表

| 维度 | 方案 A（重构） | 方案 B（Future/Promise） | 方案 C（coroutine，排除） |
|------|---------------|------------------------|------------------------|
| 标准 | C++17 ✅ | C++17 ✅（自研） | C++20 ❌（需升级） |
| 新依赖 | 无 | 自研 ~150 行库 | 无（Drogon 原生） |
| 规范改动 | 无 | 无 | 需解禁 CoroMapper |
| 接口破坏性 | 无 | **有**（4 接口签名改） | 有（同 B 量级） |
| 缩进消除 | 部分（仍单层回调） | **完全** | **完全** |
| 工作量 | 中（单文件） | 大（跨库接口） | 大（含升级） |
| 风险 | **低** | 高 | 中 |
| 可逆性 | 高 | 中 | 中 |
| 治标/治本 | 治标 | 治本 | 治本（终极） |

---

## 4. 推荐路径与理由

### 推荐：**方案 A 优先落地**，以 `GitHubController.cc` 作为唯一示范文件。

**理由**：
1. **零依赖、零规范改动、零回归面**，可立即执行，符合"先做 1 个文件"的决策。
2. 行为等价重构，可逐方法迁移 + 单测验证，风险可控。
3. **为未来方案 B/C 铺路**：Flow 状态对象 / 具名步骤方法与 coroutine 天然兼容（每步方法将来直接加 `co_await` 即可），不会白做。
4. 立即解决最痛的三个具体问题：`issueTokens` 两份副本、9 处重复错误回调、`sharedCb/callbackPtr` 层层捕获。

### GitHubController.cc 改造清单（方案 A 落地时的具体动作）

1. 抽 `issueTokens` 为成员方法 `void issueTokensForUser(int64_t userId, const std::string &username)`，消除副本 A/B。
2. 新增 `void respondDbError(const HttpRequestPtr&, const CallbackPtr&, const char *ctx, const DrogonDbException&)`，统一 9 处错误回调。
3. 新增 `GitHubLoginFlow`（或 `GitHubLoginState`）类，持有 `req/callbackPtr/db/provider/subject/githubLogin/githubEmail/accessToken`，把 fallback 7 层切成：`start()` → `exchangeToken()` → `fetchUserinfo()` → `resolveMapping()` → `linkExistingUser()` / `createNewUser()` → `issueTokens()`。
4. 收敛 `[this, callbackPtr, req, db, ...]` 多变量捕获为 `[self = shared_from_this()]` 单变量捕获（顺带修正规范违反）。
5. 复用 / 提炼 `fetchUserRoleNames` 模式，处理 `insert(UserRoles)` 的"尽力而为"语义（成功/失败都走 issueTokens）显式化。

### 方案 B / C 的触发条件（中长期演进，不在本轮）

- **方案 B 触发**：团队接受一次性破坏性接口改动，且不愿升级 C++20 时。
- **方案 C 触发**：`TECH_SPECS.md` CoroMapper 禁令解禁、CMakePresets cppstd 提到 20、且 Drogon 版本确认暴露稳定 coroutine API 时。**这是终极推荐方向**。

---

## 5. 示范文件改造预览（GitHubController.cc）

### 改造前（真实代码，最深缩进片段）

`GitHubController.cc:382` 起的 fallback 路径 `issueTokens` 副本 B 内部，refresh token insert 成功回调（约 L435–448），缩进已达 12–13 层：

```cpp
// Layer: sendRequest → sendRequest → findBy(SubjectMappings) → findBy(Users) → issueTokens(B)
//                                                   → insert(AccessTokens) → insert(RefreshTokens)
Mapper<Oauth2RefreshTokens>(db2).insert(rtModel,
  [callbackPtr, accessToken, refreshToken](const Oauth2RefreshTokens &) {
      Json::Value result;
      result["access_token"] = accessToken;
      result["token_type"] = "Bearer";
      result["expires_in"] = ...;
      result["refresh_token"] = refreshToken;
      (*callbackPtr)(::drogon::HttpResponse::newHttpJsonResponse(result));
  },
  [callbackPtr, req](const ::drogon::orm::DrogonDbException &e) {
      respondError(req, callbackPtr, "DB_QUERY_ERROR",
                   std::string("failed to create refresh token") + e.base().what());
  });
```

且同样的块在副本 A（`GitHubController.cc:195` 附近）逐字重复一次。

### 改造后（方案 A 伪代码，示意非可编译）

```cpp
// GitHubController.cc：login 方法体收缩为流程编排
void GitHubController::login(const HttpRequestPtr &req,
                             std::function<void(const HttpResponsePtr&)> &&callback) {
    auto flow = std::make_shared<GitHubLoginFlow>(
        req, std::move(callback), dbClient(), tokenService_);
    flow->start();
}

// GitHubLoginFlow（独立类，成员变量承载状态，方法承载步骤）
void GitHubLoginFlow::start()                    { exchangeToken(); }
void GitHubLoginFlow::exchangeToken()            { /* 单层 sendRequest 回调 → fetchUserinfo */ }
void GitHubLoginFlow::fetchUserinfo()            { /* 单层 sendRequest 回调 → resolveMapping */ }
void GitHubLoginFlow::resolveMapping()           { /* 单层 findBy 回调 → linkExisting / createNew */ }
void GitHubLoginFlow::linkExistingUser(int64_t id){ /* 单层 findBy(Users) → issueTokensForUser */ }
void GitHubLoginFlow::createNewUser()            { /* execSqlAsync → insert(Mapping) → insert(Role) → issueTokensForUser */ }
void GitHubLoginFlow::issueTokensForUser(int64_t userId, const std::string &username) {
    /* 统一的 access+refresh token 签发，单份实现 */
}
void GitHubLoginFlow::fail(const char *code, std::string msg) {
    respondError(req_, callback_, code, std::move(msg));
}
void GitHubLoginFlow::failDb(const char *ctx, const DrogonDbException &e) {
    respondDbError(req_, callback_, ctx, e);   // 统一的 DB 错误响应
}
```

**收益对照**：
- 564 行 → 约 300 行（净减 ~200 行，主要来自去重 + 错误回调统一）。
- 最深缩进 12–13 层 → 最深 2 层（单层回调）。
- `issueTokens` 副本：2 → 1。
- 错误回调模板：9 处重复 → 1 个 `failDb` helper。
- lambda 多变量捕获 → `[self = shared_from_this()]` 单变量捕获（顺带合规 TECH_SPECS 的 `[this]` 禁令）。

> 注：以上为报告内的**形态示意**，非本次落地代码。本次只交付本评估文档。

---

## 6. 决策清单（留给团队 / 需求方拍板）

1. **是否接受方案 A 作为本次唯一落地范围**（仅改 `GitHubController.cc`，另起 PR）？
2. **是否需要为方案 B 预留 `libs/common/Future.h` 的设计草稿**（可后续单独 PR，不在本轮）？
3. **`TECH_SPECS.md` 两处规范的处置时机**：
   - CoroMapper `[-] 禁止` 禁令 —— 是否安排单独讨论评估解禁条件？
   - `[this]` 禁令 —— 现实已普遍违反，是否修订规范使之与代码一致（例如允许 `shared_from_this` 形态）？

---

## 附录：本次评估引用的代码位置索引

| 文件 | 关键位置 |
|------|---------|
| `libs/drogon/src/controllers/GitHubController.cc` | `login` L95–L659；`callbackPtr` L150/L272；`issueTokens` 副本 A L154 / 副本 B L382；9 处 DB 错误回调 |
| `libs/identity/src/social/GitHubAuthService.cc` | `login` L26–L150，4 层嵌套；`sharedCb` L41 |
| `libs/drogon/src/admin/UserAdminService.cc` | `listUsers` L74–L186，3 层串行 Mapper；`fetchUserRoleNames` L192–L242 |
| `libs/drogon/src/adapters/DrogonOAuthHttpClient.cc` | `postForm` L32 / `getWithBearerToken` L64 |
| `libs/identity/include/fulla/identity/IUserRepository.h` | 9 个 `&&callback` 方法（L40–L125） |
| `libs/identity/include/fulla/identity/ISocialAccountRepository.h` | `LookupCallback`/`CreateCallback` L92–L93 |
| `libs/identity/include/fulla/identity/IOAuthHttpClient.h` | `ResultCallback` L86；`OAuthHttpResult` L55–L74 |
| `TECH_SPECS.md` | §一 异步规范 L22–L35；Lambda 捕获 L56–L67；C++17 L76 |
| `CMakePresets.json` | 12 份 preset 硬编码 `compiler.cppstd=17` |
| `.github/workflows/` | 6 份 CI workflow；Linux GCC 11 / Windows VS2022 / macOS Apple Clang |
