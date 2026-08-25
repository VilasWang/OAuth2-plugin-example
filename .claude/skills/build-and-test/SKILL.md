---
name: build-and-test
description: 构建和测试C++ OAuth2后端，包含正确的依赖管理
disable-model-invocation: true
---

# 构建和测试OAuth2后端

这个技能帮助您构建、测试和运行OAuth2服务器，完全模拟 `build.bat` / `build.sh` 和 CI 工作流的流程。

## 使用方法

通过用户调用：`/build-and-test [debug|release]`

## 推荐：统一管理接口

```powershell
# Windows PowerShell
.\manage.ps1 build-backend           # 默认 Release 构建
.\manage.ps1 build-backend -debug    # Debug 构建
.\manage.ps1 test-backend            # 运行后端测试（ctest，从 build/<preset> 目录）
```

```bash
# Linux/macOS
./manage.sh build-backend
./manage.sh build-backend -debug
./manage.sh test-backend
```

`manage.ps1` / `manage.sh` 是对 `scripts/backend/build.{bat,sh}`、`test.{bat,sh}` 的统一封装。直接调用脚本也可以（见下方“手动流程”）。两者都没有 `rebuild-db` 子命令——重置数据库请走 `/db-reset` 技能（直接 `psql`）。

## 构建系统事实（务必遵循）

- **构建后端走 Conan + `cmake --preset`**。`CMakePresets.json` 定义了多个 preset，每个 preset 把构建产物输出到 `build/<preset>`（例如 `build/windows-msvc`、`build/linux-release`、`build/macos-arm64`；另有 `-debug` / `-asan` / `-tsan` 变体）。
- **不要在 `build/apps/server` 或 `build/tests` 下跑 ctest**——`CTestTestfile.cmake` 在 `build/<preset>` 根目录，必须从那里运行才能覆盖 `tests/` 和 `libs/**` 的全部用例。
- **C++ 标准 = 17**（顶层 `CMakeLists.txt` 的 `CMAKE_CXX_STANDARD 17`，Conan `compiler.cppstd=17`）。不要传 `cppstd=20` / `-DCMAKE_CXX_STANDARD=20`。
- **服务器二进制**：`fulla-server`（`fulla-server.exe` on Windows）。所在路径：
  - Windows（多配置生成器）：`build/windows-msvc/apps/server/Release/fulla-server.exe`
  - Linux/macOS（单配置生成器）：`build/linux-release/apps/server/fulla-server`
- **默认监听端口 `5555`**（所有 `apps/server/config/*.json` 的 `listeners[0].port`）。
- **配置文件**：`apps/server/config/config.json`（默认）+ 覆盖文件 `config.dev.json` / `config.ci.json` / `config.prod.json` / `config.bench.json`。`config.ci.json` 使用内存存储（`storage_type="memory"`），不连 PostgreSQL。

## 完整工作流程（手动流程，作为 manage 封装的降级方案）

### 1. 环境准备

```powershell
# 停止正在运行的 fulla-server 进程（Windows）
taskkill /F /IM fulla-server.exe 2>$null || echo "No running process"
# Linux/macOS
pkill -9 fulla-server 2>$null || echo "No running process"
```

### 2. 检查项目结构

```powershell
Test-Path "apps/server"       # 应该存在
Test-Path "libs"              # 应该存在
Test-Path "scripts/backend"   # 应该存在
Test-Path "manage.ps1"        # 应该存在
Test-Path "CMakePresets.json" # 应该存在
```

### 3. 依赖安装（Conan）

Conan 在仓库根目录运行，把依赖安装到对应 preset 的输出目录：

```bash
# Windows MSVC (Release)
conan install . --output-folder=build/windows-msvc -s build_type=Release -s compiler.cppstd=17 --build=missing

# Linux GCC (Release)
conan install . --output-folder=build/linux-release -s build_type=Release -s compiler.cppstd=17 --build=missing

# macOS Clang ARM64 (Release)
conan install . --output-folder=build/macos-arm64 -s build_type=Release -s compiler.cppstd=17 --build=missing
```

### 4. CMake 配置与构建

```bash
# 配置（从仓库根目录，使用 preset 名称）
cmake --preset windows-msvc      # 或 linux-release / macos-arm64 / 对应 -debug 变体

# 构建
# Windows（多配置，需要 --config）
cmake --build build/windows-msvc --parallel --config Release
# Linux/macOS（单配置）
cmake --build build/linux-release --parallel
```

构建产物（含 `fulla-server` 二进制和 `config.json` 副本）位于 `build/<preset>/apps/server/...`。

### 5. 测试环境准备（集成测试需要 PostgreSQL + Redis）

```bash
pg_isready -h localhost -p 5432 || echo "PostgreSQL not ready"
redis-cli -h localhost -p 6379 ping || echo "Redis not ready"

# 初始化测试数据库（本地 / 开发）：用统一脚本
./scripts/backend/setup-database.sh     # Linux/macOS
# 或
scripts\backend\setup_database.bat      # Windows
# 等价于 /db-reset：DROP+CREATE fulla_db，应用 apps/server/migrations/V001..V024，再导入 apps/server/seed/*.sql
```

### 6. 运行测试（ctest）

```bash
# 从 preset 构建根目录运行（关键：不是 build/apps/server 或 build/tests）
cd build/windows-msvc          # 或 build/linux-release
ctest --output-on-failure
# 或按标签筛选
ctest -L Unit                  # Unit / Integration / E2E / Security / Performance / Contract / API / Database / Acceptance
# 详细输出
ctest -V --output-on-failure
```

等价封装：`./manage.sh test-backend` 或 `scripts/backend/test.sh`。

### 7. 运行服务器（可选）

```bash
# Windows
build/windows-msvc/apps/server/Release/fulla-server.exe
# Linux/macOS
build/linux-release/apps/server/fulla-server
```

或用 `.\manage.ps1 run-backend` / `scripts/backend/run-server.sh`。

## 平台差异

### Windows
- 使用 `build.bat` / `manage.ps1`，MSVC 编译器（VS 2022+）
- 多配置生成器：构建需 `--config Release|Debug`
- 二进制：`fulla-server.exe`，位于 `build/windows-msvc/apps/server/Release/`

### Linux/macOS
- 使用 `build.sh` / `manage.sh`，GCC/Clang 编译器
- 单配置生成器：构建目录即目标配置
- 二进制：`fulla-server`，位于 `build/<preset>/apps/server/`

## 测试架构

- **测试二进制**：后端所有 Drogon 测试用例编译进单个 `fulla-tests` 二进制，注册为 ctest 条目 `OAuth2Tests`（约 558 个 `DROGON_TEST` 用例，其中 84 个 Contract 用例）。Contract 用例另外以 `Contract.*`（84 条）独立 ctest 条目注册，便于 `ctest -L Contract`。
- **进程外端点测试**：`EndpointTests_OutOfProcess`（1 条）通过 `scripts/backend/run-endpoint-tests.{sh,ps1}` 启动 server 并跑 `test-oauth2-endpoints`（59 个测试）+ `test-admin-endpoints`（52 个测试）。
- **库单元测试**：`libs/**` 下的 gtest 套件（如 `fulla-oauth2-test` 等，约 369 条 ctest 条目）。
- **SDK 冒烟**：`SdkSmoke.FullStack`（1 条）驱动 `examples/full-stack-host`。
- 全仓 ctest 条目约 450+；仅后端 `tests/` 子树为 86 条。
- 测试分类标签（见 `tests/common/test_categories.h`）：`Unit`、`Integration`、`E2E`、`Performance`、`Security`、`API`、`Database`、`Acceptance`，以及优先级 `P0`–`P3`。

## 故障排除

### 构建问题
- **Conan 安装失败**：检查网络和 Conan 配置；确认 `conanfile.py` 在仓库根。
- **CMake 配置失败**：确保对应 preset 的 `build/<preset>/conan_toolchain.cmake` 已存在（先跑 `conan install`）。
- **编译错误**：检查 C++ 标准（应为 17）和编译器版本。

### 测试问题
- **数据库连接失败**：检查 PostgreSQL 服务与 `apps/server/config/config.dev.json` 的 db 配置；CI 用内存存储，不连 PG。
- **Redis 连接失败**：确保 Redis 服务运行（本地 6379；Docker 6380）。
- **ctest 找不到用例**：必须从 `build/<preset>` 根目录运行，不要在子目录运行。
- **权限错误**：验证数据库用户权限和 schema。

### 进程问题
- **端口占用**：fulla-server 默认使用 5555 端口。
- **文件锁定**：确保旧进程已完全停止（taskkill / pkill）。

## 最佳实践

1. **开发时**：使用 `/build-and-test debug` 快速迭代。
2. **提交前**：使用 `/build-and-test release` 跑完整测试。
3. **CI 调试**：加 `-V` 获取详细测试输出。
4. **性能测试**：`AdvancedStorageTest` 是 `fulla-tests` 内的一个 `DROGON_TEST`（不是独立二进制），用 `ctest -R AdvancedStorage` 运行。

## 注意事项

- 首次构建需要较长时间（下载 Conan 依赖）。
- 集成测试前确保 PostgreSQL + Redis 在运行（或本地用 `manage.ps1 docker-up` 拉起 Docker 栈）。
- Windows 开发推荐 Visual Studio 2022 或更新版本。
- 测试会修改数据库，建议使用专用测试数据库。
