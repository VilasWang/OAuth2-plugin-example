# 项目优化实施计划 (Implementation Plan)

## 1. 概览
本计划旨在通过对项目进行架构重构、结构优化和工程化改进，提升代码的可维护性、性能以及开发效率。

## 2. 实施目标
- **解耦**：将 `OAuth2Controller` 的业务逻辑抽离至 Service 层。
- **规范**：统一文档、脚本和 Docker 构建流程。
- **性能**：优化缓存机制，提升高并发下的响应速度。
- **健壮**：完善前端 Token 管理和后端错误拦截机制。

---

## 3. 详细实施路线图

### 第一阶段：工程基础与结构优化 (预期：1-2天)
*重点：清理“代码债务”，统一工程规范。*

1.  **文档与脚本整合**：
    *   将 `OAuth2Backend/docs/` 整合至根目录 `docs/`。
    *   合并根目录与后端目录的 `scripts/`，建立统一的 CLI 入口。
2.  **Docker 镜像现代化**：
    *   编写统一的 `Dockerfile`（多阶段构建），替代原有的 `Dockerfile.debug`, `Dockerfile.cn` 等。
    *   优化 `docker-compose.yml`，统一环境变量管理。
3.  **CMake 规范化**：
    *   将 `CMakeLists.txt` 重构为基于 Target 的现代写法，移除过时的全局配置。

### 第二阶段：核心架构重构 - 深度插件化与 Service 层抽离 (预期：4-5天)
*重点：将 OAuth2 核心功能彻底解耦为独立插件，并实现 Service 层。*

1.  **定义独立插件结构 (Core Plugin Library)**：
    *   将 `OAuth2Plugin`、`Storage` 接口及实现、核心 `Models` 以及 `common/` 工具库打包为独立的 CMake 项目或库目标。
    *   **真正插件化**：支持 `cmake --install`，将插件头文件和库文件安装到系统或 Drogon 指定目录下。
2.  **创建 Server Demo 程序**：
    *   将 `OAuth2Backend` 改造为一个典型的工程示例，通过 `find_package(OAuth2Plugin)` 或直接链接插件库的方式使用功能。
    *   Controller 层应支持配置化加载，允许用户选择性启用哪些 OAuth2 端点。
3.  **Service 层抽离**：
    *   在插件库内部实现 `services/TokenService`, `services/ClientService` 等，作为插件对外的核心 API。
4.  **重构 OAuth2Controller**：
    *   Controller 演变为插件库的可选组件或在 Demo 中引用插件 Service 的薄层。


### 第三阶段：全栈增强与安全优化 (预期：2天)
*重点：提升用户体验与系统安全性。*

1.  **标准化错误拦截 (Backend)**：
    *   实现全局 `ExceptionHandler`，自动将 C++ 异常转化为符合 OAuth2 规范的 JSON 响应。
2.  **前端 Token 自动续期 (Frontend)**：
    *   在 `src/utils/axios.js`（或对应库）中实现响应拦截器，捕获 401 状态并自动使用 `refresh_token`。
3.  **缓存优化 (Storage)**：
    *   为 `CachedOAuth2Storage` 增加本地热点数据缓存，进一步降低 Redis 压力。

### 第四阶段：测试、验证与交付 (预期：1-2天)
*重点：确保重构不引入回归。*

1.  **自动化测试恢复**：
    *   运行现有 `ctest`，确保 Service 层重构后 100% 通过。
2.  **集成与压力测试**：
    *   通过 `docker-compose` 启动全栈环境，运行 E2E 授权流测试。
3.  **文档更新**：
    *   根据新的架构图更新 `architecture_overview.md`。

---

## 4. 风险评估与对策

| 风险点 | 影响程度 | 对策 |
| :--- | :--- | :--- |
| **重构导致 API 不兼容** | 高 | 严格保持 `OAuth2Controller` 对外的 URL 和参数不变，通过单元测试验证。 |
| **Docker 构建失败** | 中 | 采用多阶段构建时，先在本地环境中验证每一层，保留旧版 Dockerfile 备用。 |
| **异步竞争问题** | 高 | C++ 重构过程中，重点审查 Lambda 捕获和 `shared_from_this` 的使用。 |

## 5. 验收标准
- [ ] 后端单元测试通过率 100%。
- [ ] Docker 镜像体积显著缩小（预期减少 30%+）。
- [ ] 前端在 Access Token 过期后能无感完成授权续期。
- [ ] 目录结构符合 RFC 样式的工程规范。
