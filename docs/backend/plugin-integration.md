# OAuth2 Plugin 集成指南

本文档介绍如何将本项目的 `OAuth2Plugin` 集成到其他的 Drogon 应用程序中。在 SDK 重构后的架构中，插件随 `fulla::drogon` 包发布，宿主通过 `find_package(fulla-drogon)` 链接，无需复制源代码。完整发布物与版本契约见 [SDK 集成指南](sdk-integration-guide.md)。

## 1. 库结构

插件位于 SDK 包 `fulla::drogon`（CMake 目标 `fulla-drogon`，源码在 `libs/drogon/`），其结构如下：

* `CMakeLists.txt` — 包的构建系统（`project(fulla-drogon)`）
* `include/fulla/drogon/` — 对外暴露的头文件接口（插件声明、配置管理、数据类型）
* `src/` — 核心实现：
    * `plugin/OAuth2Plugin.cc` — Drogon 插件生命周期与初始化
    * `controllers/` — 自动注册的 OAuth2 协议端点（如 `/oauth2/token`）
    * `filters/` — 安全拦截器（`AuthorizationFilter` / `OAuth2AuthFilter`）
    * 各存储后端在独立的 `libs/storage-{memory,redis,postgres}/` 包中实现，按需链接

## 2. 集成步骤

### 第一步：解析依赖并 find_package

按 [README](../../README.md#path-c--consume-as-an-sdk) 的方式用 Conan 解析依赖（仓库的 `conanfile.py` + `conan.lock`），然后在宿主的 `CMakeLists.txt` 中：

```cmake
# 全栈：一个包拉入引擎 + Drogon 插件/controllers/views
find_package(fulla-drogon CONFIG REQUIRED)
```

### 第二步：链接目标库

在你的宿主应用目标（例如 `YourServerApp`）的 `CMakeLists.txt` 中，链接 `fulla::drogon` 目标：

```cmake
target_link_libraries(YourServerApp PRIVATE
    Drogon::Drogon
    fulla::drogon
)
```
CMake 将自动处理 include 路径和编译依赖。

### 第三步：配置 config.json

插件会自动注册协议路由和 Filter。你只需在宿主应用的 `config.json` 中配置插件以激活它（以 PostgreSQL 为例）：

```json
{
    "plugins": [
        {
            "name": "OAuth2Plugin",
            "dependencies": [],
            "config": {
                "storage_type": "postgres",
                "postgres": {
                    "db_client_name": "default"
                },
                "redis": {
                    "client_name": "default"
                }
            }
        }
    ]
}
```

### 第四步：使用拦截器保护业务 API

在你的业务 Controller 中，挂载 `AuthorizationFilter` 即可保护 API（Filter 的全限定名为 `fulla::drogon::filters::AuthorizationFilter`）：

```cpp
#include <drogon/HttpController.h>

class UserApi : public drogon::HttpController<UserApi>
{
  public:
    METHOD_LIST_BEGIN
    // 自动被 OAuth2Plugin 校验 Bearer Token
    ADD_METHOD_TO(UserApi::getProfile, "/api/me", drogon::Get,
                  "fulla::drogon::filters::AuthorizationFilter");
    METHOD_LIST_END
    // ...
};
```

## 3. 注意事项

1. **自动注册**：只要 `fulla::drogon` 被链接到最终的二进制文件中，其包含的 Controller 和 Filter 就会被 Drogon 框架在启动时自动注册。请不要在 `main.cc` 中手动调用初始化宏。
2. **数据库初始化**：使用 PostgreSQL 存储时，确保宿主应用启动前已执行 `apps/server/migrations/` 下的迁移脚本（或启用后端的 `FULLA_AUTO_MIGRATE=true` 自动执行）。
