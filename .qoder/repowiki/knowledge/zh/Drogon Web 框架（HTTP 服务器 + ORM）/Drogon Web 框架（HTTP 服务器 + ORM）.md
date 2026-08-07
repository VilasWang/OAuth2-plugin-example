---
kind: external_dependency
name: Drogon Web 框架（HTTP 服务器 + ORM）
slug: drogon
category: external_dependency
category_hints:
    - framework_behavior
    - client_constraint
scope:
    - '**'
source_files:
    - libs/drogon/CMakeLists.txt
    - apps/server/src/bootstrap/ControllerRegistration.cc
    - apps/server/views/login.csp
---

### Drogon
- **角色**：项目 HTTP 服务器、控制器注册、CSP 视图渲染、`drogon::orm` 的 ORM 层；OAuth2Plugin 作为 Drogon 插件通过 `plugins[].name="OAuth2Plugin"` 反射实例化。
- **集成点**：SDK 包 `authforge::drogon` 导出 Drogon 目标，并 PUBLIC 传递 common/oauth2/identity/storage-*/OpenSSL/CURL 依赖闭包；`apps/server/src/bootstrap/ControllerRegistration.cc` 完成路由装配，`apps/server/views/login.csp` 由 `drogon_create_views` 编译进产物。
- **方向**：全栈 smoke 仅链 `authforge::drogon` 一个目标即可拉起 OAuth2 授权码流，无需 whole-archive。