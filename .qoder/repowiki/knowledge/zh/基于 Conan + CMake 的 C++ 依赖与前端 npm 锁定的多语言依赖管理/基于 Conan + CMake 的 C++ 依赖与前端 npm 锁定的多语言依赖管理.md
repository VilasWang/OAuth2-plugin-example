---
kind: dependency_management
name: 基于 Conan + CMake 的 C++ 依赖与前端 npm 锁定的多语言依赖管理
category: dependency_management
scope:
    - '**'
source_files:
    - conanfile.py
    - conan.lock
    - CMakeLists.txt
    - cmake/AuthForgePackage.cmake
    - cmake/Version.cmake
    - cmake/Paths.cmake
    - .github/workflows/_build-test.yml
    - .github/workflows/_frontend.yml
    - .github/workflows/_sdk-smoke.yml
    - .github/workflows/security.yml
    - .github/workflows/release.yml
    - frontends/admin/package.json
    - frontends/admin/package-lock.json
    - frontends/user/package.json
    - frontends/user/package-lock.json
    - tools/security/dependency_eol_check.py
---

## 1. 使用的系统/方法

仓库采用**分层式多工具链**管理依赖：
- **C++ 后端与 SDK**：使用 **Conan 2**（`conanfile.py`）作为唯一声明入口，通过 `CMakeToolchain` + `CMakeDeps` 生成 CMake 配置文件，由根 `CMakeLists.txt` 消费。测试依赖通过 `test_requires` 单独声明。
- **前端（Vue3）**：两个独立应用 `frontends/admin` 与 `frontends/user` 各自维护 `package.json` + `package-lock.json`，CI 统一使用 `npm ci` 安装。
- **构建编排**：根级 `CMakePresets.json`、`paths.env`、`cmake/*.cmake` 模块把 Conan 解析结果桥接到 CMake 变量（如 `WITH_IDENTITY` / `WITH_SOCIAL` / `WITH_WEBAUTHN`），实现“一份选项，两处生效”。

## 2. 关键文件

| 作用 | 文件 |
|---|---|
| C++ 依赖声明与选项映射 | `conanfile.py` |
| C++ 依赖锁定 | `conan.lock` |
| 顶层 CMake 入口（含条件编译选项） | `CMakeLists.txt` |
| Conan→CMake 工具链集成 | `cmake/AuthForgePackage.cmake`、`cmake/Version.cmake`、`cmake/Paths.cmake` |
| CI Conan 缓存键 | `.github/workflows/_build-test.yml`、`.github/workflows/_sdk-smoke.yml` |
| 前端依赖声明 | `frontends/admin/package.json`、`frontends/user/package.json` |
| 前端依赖锁定 | `frontends/admin/package-lock.json`、`frontends/user/package-lock.json` |
| 前端 CI 安装 | `.github/workflows/_frontend.yml` |
| 依赖 EOL 安全扫描 | `tools/security/dependency_eol_check.py` + `.github/workflows/security.yml` |
| 发布时 SBOM/清单 | `.github/workflows/release.yml` |

## 3. 架构与约定

### 3.1 C++ 依赖（Conan + CMake）

- `conanfile.py` 是**单一事实来源**（注释明确替代了旧版 `conanfile.txt`），声明了 Drogon 1.9.13、OpenSSL 3.5.7、jsoncpp 1.9.5、hiredis 1.2.0、libcurl 8.6.0、brotli 1.1.0、zlib 1.3.1，以及 gtest 1.14.0（仅测试）。OpenSSL 与 zlib 使用 `override=True` 强制全项目统一 TLS 栈。
- 通过 `default_options` 集中控制第三方包开关：`drogon/*:with_orm/postgres/redis/ctl/sqlite/brotli` 与 `libcurl/*:with_ssl=openssl`，避免下游重复配置。
- 可选功能面 `with_identity` / `with_social` / `with_webauthn` 在 `generate()` 中映射为 CMake 缓存变量 `WITH_*`，使同一份源码可按需裁剪依赖面（例如关闭 WebAuthn 可移除 libcbor 等 crypto 依赖）。
- 根 `CMakeLists.txt` 在 `project()` 之后立即 `include(Version)`、`Compatibility`、`Sanitizers`、`Coverage`、`Warnings`、`Paths` 等共享模块，并按设计文档的分层顺序 `add_subdirectory` libs/common → oauth2 → identity → storage-* → drogon → apps/server → tests → examples。
- 示例工程 `examples/third-party-host` 通过 `find_package(authforge)` 消费已安装的 SDK；`examples/full-stack-host` 通过 `ctest --build-and-test` 用 `CMAKE_PREFIX_PATH=${CMAKE_BINARY_DIR}/authforge-cmake` 验证真实外部消费者路径。

### 3.2 前端依赖（npm）

- 每个前端子目录独立 `package.json`，devDependencies 包含 Vite、TypeScript、Vitest、Playwright、TailwindCSS 等；运行时依赖集中在 Vue/Pinia/Axios 等。
- CI 使用 `npm ci`（而非 `npm install`），配合 `package-lock.json` 保证确定性安装。
- Dockerfile (`frontends/admin/Dockerfile`) 同样以 `npm ci` 构建镜像。

### 3.3 CI 中的依赖缓存与锁定

- Conan 缓存键形如 `conan-${{ runner.os }}-v1-cpp17-${{ hashFiles('conanfile.py', 'conan.lock') }}`，同时哈希 `conanfile.py` 与 `conan.lock`，确保 lock 变更触发缓存失效。
- 前端 CI 使用 `npm ci`，天然依赖 `package-lock.json` 的完整性校验。
- 发布流程（`release.yml`）会收集 `conan.lock` 与两份 `package-lock.json` 一起生成 SBOM（SPDX JSON）并上传 Release 附件。

## 4. 约定与约束

- **C++ 依赖必须经 Conan 声明**：README.zh-CN 明确要求“第三方依赖请用仓库的 `conanfile.py` + `conan.lock` 解析”，禁止直接 `apt`/`brew`/`vcpkg` 引入。
- **TLS 栈统一**：`libcurl/*:with_ssl=openssl` 强制所有 curl 使用者走 OpenSSL，避免混合 TLS 后端。
- **可选特性即依赖门控**：`WITH_IDENTITY` / `WITH_SOCIAL` / `WITH_WEBAUTHN` 默认 ON，设为 OFF 会跳过对应源码编译，从而缩小最终二进制依赖面（conanfile 与 CMake 双向联动）。
- **测试依赖隔离**：gtest 通过 `test_requires` 声明，不进入生产依赖图。
- **前端必须使用 `npm ci`**：CI 与 Docker 构建均使用 `npm ci`，禁止交互式 `npm install`。
- **依赖 EOL 门禁**：`tools/security/dependency_eol_check.py` 离线解析 `conan.lock` 中全部 requires+overrides（约 20 个锁定引用），对照内置策略（openssl≥3.0、zlib≥1.2.13、libcurl≥8.4.0 等 CVE/EOL 线），在 `security.yml` 的 push/PR + 每周 cron 运行，失败则阻断流水线。
- **无私有注册表**：当前未发现 `.conanrc`、`CONAN_REPOS_*` 或 npm registry 重定向配置；依赖全部来自 Conan Center 与官方 npm registry。
- **版本锁定粒度**：C++ 依赖使用精确版本号（如 `drogon/1.9.13`、`openssl/3.5.7`），前端 devDependencies 部分使用固定版本（如 `fast-check: 4.8.0`、`vitest: 4.1.7`），运行时依赖使用 `^` 范围但由 `package-lock.json` 锁定实际树。