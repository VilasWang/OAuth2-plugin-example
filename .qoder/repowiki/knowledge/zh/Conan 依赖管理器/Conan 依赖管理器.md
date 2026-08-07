---
kind: external_dependency
name: Conan 依赖管理器
slug: conan
category: external_dependency
category_hints:
    - framework_behavior
    - client_constraint
scope:
    - '**'
source_files:
    - CMakePresets.json
    - conanfile.py
    - scripts/backend/build.sh
    - scripts/backend/build.bat
---

### Conan
- **角色**：统一三平台依赖拉取与 toolchain 生成（Drogon、OpenSSL、JsonCpp、hiredis 等），替代 Linux/macOS 原生的系统包安装路径。
- **集成点**：`CMakePresets.json` 中每个 preset 指向 `build/<preset>/conan_toolchain.cmake`；`conan install --output-folder=build/<preset>` 在构建前执行；CI 三平台 workflow 均改为 preset 驱动。
- **约束**：Conan 生成的 `CMakeUserPresets.json` 会 include 扁平 `build/CMakePresets.json`，若残留会导致 duplicate preset 错误——构建脚本需在 `conan install` 前删除根目录该文件。
- **方向**：所有本地/CI 构建统一走 `cmake --preset`，不再硬编码 `build/` 扁平目录。