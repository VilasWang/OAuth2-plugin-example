# SDK Runtime Contract

对外契约声明：AuthForge SDK（`authforge::common` / `authforge::oauth2` /
`authforge::identity` / `authforge::storage-*` / `authforge::drogon`）在 v1.x
期间对消费者承诺的线程模型、ABI、异常、日志与依赖边界。

来源：`.kiro/specs/authforge-sdk-refactor/design.md`（评审 F9、H1 §5.7）。
本文档是对外承诺的单一出处；SDK 头文件注释与本文冲突时以本文为准并修头。

---

## 1. 线程模型

- Domain 服务（`libs/oauth2`、`libs/identity`）**不自持事件循环**；所有异步
  操作经回调返回。
- **回调可能在任意 Drogon IO 线程触发，不保证是调用线程**。消费者不得假设
  线程亲和性；需要回到特定线程时由消费者自行投递。
- 只读单例（如 `JwkManager`）遵循 **init-once-then-read-only**：在服务开始
  接受请求前完成一次性 `init()`，之后以 `shared_ptr<const T>` 发布，运行期
  不再变更。消费者在 SDK 装配完成后并发读取是安全的。
- 服务对象持有 `shared_ptr` 仓储句柄保证生命周期；异步续体一律捕获
  `auto self = shared_from_this()`，禁止 `[this]` / `[&]`。

## 2. ABI 稳定性

- v1.x **仅支持 `find_package` 源码集成，不承诺二进制 ABI**。
- 语义化版本只覆盖**源码级 API**：公共头 `include/authforge/**` 遵循
  SemVer，破坏性变更必须升 major（由 api-diff 工具在 CI 强制）。
- 跨编译器 / 跨 STL 混用预编译二进制不在支持范围；进入 Conan 二进制包
  分发阶段后再单独制定 ABI 策略。
- 弃用流程：`[[deprecated]]` 标注 + 至少一个 minor 周期过渡后方可移除。

## 3. 异常安全约定

- Domain 公共 API 以 `Result<T, Error>` 返回**可预期错误**，不用异常表达
  业务失败。
- 仅在不可恢复的编程错误（契约违反、断言级问题）抛异常。
- 存储底层异常（如 `DrogonDbException`）**必须在 Adapter 层
  （`libs/storage-*`、`libs/drogon`）捕获并转为 `Error`，不得穿透到
  Domain 回调**。消费者在 Domain 回调内无需 try/catch 存储异常。

## 4. 日志抽象

- Domain 代码经 `common::ports::ILogger` 端口输出日志，**不直接使用
  Drogon `LOG_*` 宏**（arch-guard 强制 Domain 层不 include drogon 头）。
- SDK 默认提供 Drogon 日志适配实现（`libs/drogon` Adapter）；脱离 Drogon
  宿主的消费者可注入自己的 `ILogger` 实现替换。

## 5. 依赖声明

- 特性面依赖由根 `conanfile.py` 的 **`with_webauthn` / `with_identity` /
  `with_social` option 显式 gate**，不作为传递依赖的隐式惊喜。
  NOTE: 真实 WebAuthn（FIDO2）需要的 CBOR 解码依赖（`libcbor`）曾在此
  声明，但当前 WebAuthn 控制器是非加密 stub（不消费任何 CBOR），
  `libcbor` 已作为死依赖移除；待真实 WebAuthn 加密落地时需重新添加
  （详见 `conanfile.py` 对应注释）。
- 关闭选项（如 `-o with_webauthn=False`）会同步映射到 CMake 侧
  `WITH_*` 变量并裁剪对应编译面，消费者可据此收缩依赖表面。

## 6. 插件注册与配置契约（宿主集成）

- `OAuth2Plugin` 类名与 config `plugins[].name` 反射加载契约**保持稳定**
  （方案 A）：4 份 config（`config.{json,dev,ci,prod}.json`）中
  `"plugins":[{"name":"OAuth2Plugin","config":{...}}]` 的类名字符串与
  `config{}` 块 schema 是产品配置契约的一部分，v1.x 不改名。
- 插件本体以 CMake **OBJECT 库**形态链接进宿主：目标文件逐个直接链入，
  不存在静态库按需抽取丢弃自注册符号的问题，因此**当前不需要
  whole-archive**。
- 若未来改为**静态库形态分发**插件本体，链接器可能裁剪插件自注册符号导致
  Drogon 反射 "plugin not found"——届时必须引入 whole-archive（或等价
  强制链接方案）；`OAuth2Plugin` 无 `AutoCreation` 参数可用，不适用免
  whole-archive 方案。
- 第三方宿主集成示例见 `examples/third-party-host/`。
