---
kind: build_system
name: CMake + Conan 多平台构建与 CI/CD 流水线
category: build_system
scope:
    - '**'
source_files:
    - CMakeLists.txt
    - CMakePresets.json
    - conanfile.py
    - cmake/AuthForgePackage.cmake
    - cmake/Version.cmake
    - cmake/Paths.cmake
    - cmake/Sanitizers.cmake
    - cmake/Coverage.cmake
    - cmake/Warnings.cmake
    - paths.env
    - scripts/backend/build.sh
    - scripts/backend/test.sh
    - .github/workflows/ci.yml
    - .github/workflows/_build-test.yml
    - deploy/docker/Dockerfile
---

## 1. 构建系统与工具链

AuthForge 采用 **CMake 3.21+** 作为核心构建系统，配合 **Conan 2.x** 管理 C/C++ 依赖（Drogon、OpenSSL、jsoncpp、hiredis、libcurl、brotli、zlib、gtest 等），通过 `conanfile.py` 中的 `CMakeToolchain` + `CMakeDeps` 生成 `conan_toolchain.cmake` 和 `*-data.cmake` 文件供 CMake 消费。所有平台（Linux、Windows MSVC、macOS arm64）统一走同一套脚本与 preset。

- **版本来源**：`cmake/Version.cmake` 集中定义 `OAUTH2_PROJECT_VERSION_MAJOR/MINOR/PATCH`，根 `CMakeLists.txt` 的 `project(authforge VERSION 1.0.0)` 与之保持一致；`cliff.toml` 用于变更日志生成。
- **路径来源**：`paths.env` 是仓库级单一事实源，被 `cmake/Paths.cmake` 解析为 `OAUTH2_PATH_*` 变量，同时被 Bash/PowerShell/Batch 脚本共享，避免路径重复维护。
- **可选特性门控**：顶层 `option(WITH_IDENTITY|WITH_SOCIAL|WITH_WEBAUTHN)` 在 `conanfile.py` 的 `generate()` 中映射到 `tc.variables["WITH_*"]`，实现 Conan 选项 → CMake 编译开关的双向打通。

## 2. 关键文件与模块

- **顶层构建入口**：`CMakeLists.txt`（声明 C++17、启用 testing、按设计文档顺序 `add_subdirectory` libs→apps/server→tests→examples）、`CMakePresets.json`（linux-release/debug/asan/tsan、windows-msvc、macos-arm64 等 preset，含 `binaryDir` 隔离）。
- **依赖描述**：`conanfile.py`（锁定 Drogon 1.9.13、OpenSSL 3.5.7、gtest 1.14.0 等，并通过 `default_options` 固定 Drogon 子选项如 `with_orm`/`with_postgres`/`with_redis`/`with_ctl`/`with_sqlite`/`with_brotli` 以及 libcurl TLS 栈为 OpenSSL）。
- **CMake 工具集**（`cmake/`）：
  - `AuthForgePackage.cmake` + `AuthForgePackageConfig.cmake.in`：每个 `libs/*` 调用 `authforge_package(TARGET ... EXPORT_NAME ... DEPENDENCIES ...)` 同时产出 install-tree 与 build-tree (`${CMAKE_BINARY_DIR}/authforge-cmake/<pkg>/`) 的 `find_package()` 可消费配置，使 `examples/full-stack-host` 能通过 `ctest --build-and-test` 以真实外部消费者方式验证 SDK。
  - `Sanitizers.cmake`：提供 `OAUTH2_SANITIZER=off|thread|address` 缓存选项，仅对 GCC/Clang Debug 生效，TSan/ASan 互斥。
  - `Coverage.cmake`：暴露 `oauth2_apply_gcov(target)` 与 `OAUTH2_TEST_COVERAGE` 选项，对静态库本身进行 gcov 插桩。
  - `Warnings.cmake`：暴露 `oauth2_apply_warnings(target)` 与 `AUTHFORGE_WERROR` 选项（CI 强制开启）。
  - `Paths.cmake`：解析 `paths.env` 并设置 `CMAKE_CONFIGURE_DEPENDS` 使其随该文件变化重配。
- **本地构建脚本**：`scripts/backend/build.sh` / `build.bat` 封装 `conan install . --output-folder=build/<preset>` + `cmake --preset <preset>` + `cmake --build --preset <preset>`，自动处理 drogon_ctl PATH、config 拷贝、sanitizer 快捷参数（`--asan`/`--tsan`）。
- **测试运行**：`scripts/backend/test.sh` / `test.bat` 用 ctest 跑两遍——先用默认 `config.json`，再替换为 `config.ci.json`（内存存储后端）。

## 3. 架构与约定

- **分层 add_subdirectory 顺序**：`libs/common` → `libs/oauth2` → `libs/identity` → `libs/storage-*` → `libs/drogon` → `apps/server` → `tests` → `examples`，严格遵循 Domain/Adapter 依赖方向（Domain 层不依赖 Drogon）。
- **独立可消费包**：每个 `libs/*` 通过 `authforge_package()` 导出 `authforge::<alias>` 目标（如 `authforge::common`、`authforge::storage::memory`），examples/third-party-host 仅链接 SDK 包验证授权码流程，full-stack-host 通过 `find_package` 消费完整产品栈。
- **多构建目录隔离**：`CMakePresets.json` 的 `binaryDir` 将 Linux/macOS/Windows 各 preset 输出隔离到 `build/linux-release`、`build/windows-msvc`、`build/macos-arm64` 等目录，避免并行构建冲突。
- **Docker 多阶段镜像**：`deploy/docker/Dockerfile` 复用 `scripts/backend/build.sh` 保证容器内构建与主机/CI 一致；backend 使用 Conan 静态链接 C/C++ 依赖，运行时镜像仅保留 `ca-certificates` 与 `curl`。
- **前端构建**：`frontends/admin`、`frontends/user` 各自基于 Vite + npm，由 Dockerfile Stage 5/6 或 CI `_frontend.yml` 独立构建。

## 4. CI/CD 流水线（GitHub Actions）

`ci.yml` 定义三阶段 gate 链式执行：

1. **FAST gate**（static-checks + frontend property tests）：无编译，检查 arch-guard（Domain 层不得 include `<drogon/...>`）、migration-check（V<NNN> 连续命名、幂等、不可变校验和）、api-diff（SDK 头表面基线）、OpenAPI spec 校验、测试命名规范、manage.sh/ps1 命令一致性。
2. **MAIN gate**（build-test）：矩阵覆盖 linux/ubuntu-22.04、windows/windows-2022、macos/macos-14，均执行 `conan install` → `cmake --preset` → `cmake --build` → `ctest`；Linux 额外启动 Postgres + Redis 服务容器并应用 migrations/seed；Windows/macOS 使用 memory storage 配置。
3. **RELEASE gate**（sdk-smoke）：通过 `ctest --build-and-test` 以外部消费者方式重新构建 `examples/full-stack-host`，验证 `find_package` 链路。

可复用工作流 `_build-test.yml` 通过 inputs 参数化平台差异（`cmake_preset`、`preset_dir`、`configure_extra_args`、`use_database`、`run_named_test_gates` 等），并在失败时上传 `Testing/` 与 `apps/server/logs/` 产物。

## 5. 约束与规则

- C++ 标准固定为 **C++17**（`CMAKE_CXX_STANDARD 17`，Conan 安装时 `-s compiler.cppstd=17`）。
- **警告即错误**：CI 通过 `-DAUTHFORGE_WERROR=ON` 强制开启，本地可通过 `--debug`/`--release` 选择构建类型。
- **Sanitizer 互斥**：`OAUTH2_SANITIZER` 为单值枚举（off/thread/address），TSan 与 ASan 不能同时启用；MSVC 上仅发出 WARNING 而不实际插桩。
- **迁移文件命名**：必须为 `V<NNN>__<desc>.sql` 零填充连续编号，且需满足幂等与非破坏性约束（由 `tools/migration-check/migration_check.py` 在 CI 中校验）。
- **SDK API 表面守护**：`tools/api-diff/api_diff.py` 对比 `libs/*/include/authforge` 与 `tools/api-diff/api-baseline.txt`，任何破坏性变更需先提升主版本号。
- **Domain 层架构守卫**：`tools/arch-guard/arch_guard.py` 禁止 Domain 库（common/oauth2/identity）包含 `<drogon/...>` 或相互 include。
- **配置切换**：测试默认使用 `apps/server/config/config.json`，CI 通过 `config.ci.json`（内存存储）覆盖，`scripts/backend/test.sh` 会先后以两份配置运行 ctest。
- **Docker 构建一致性**：Dockerfile 直接调用 `scripts/backend/build.sh Release`，禁止手写 cmake 命令以免与 host/CI 行为漂移。