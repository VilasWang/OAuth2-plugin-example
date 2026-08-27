# 任务 A：修复 coverage workflow（#105）

## 问题诊断

### CI 失败运行分析

查看 `gh run view 33065661202`（最新一次 failure 运行，16m18s）：

```
JOBS
X gcov line coverage (linux) in 16m0s (ID 98495029421)
  ✓ Set up job
  ✓ Run actions/checkout
  ✓ Install build dependencies
  ✓ Cache Conan packages
  ✓ Configure CMake using Conan (Debug + FULLA_TEST_COVERAGE=ON)
  X Build                    ← 失败在此步骤
  - Start PostgreSQL and Redis  (未执行)
  - Run tests                   (未执行)
  - Generate gcov JSON reports  (未执行)
```

### 根因

Build 步骤在 55% 处失败，错误信息：

```
/bin/sh: 1: drogon_ctl: not found
gmake[2]: *** [apps/server/CMakeFiles/fulla-server.dir/build.make:81: apps/server/login.h] Error 127
```

`drogon_create_views()` CMake 宏在构建时调用 `drogon_ctl` 编译 `.csp` 视图文件。
Conan 安装的 Drogon 将 `drogon_ctl` 放在包缓存目录（`~/.conan2/p/.../bin/`），
不在系统 `$PATH` 中。

### 前一次运行（33064800547）

被 concurrency 取消（"Canceling since a higher priority waiting request for
coverage-refs/pull/115/merge exists"），非真实失败。

## 修复方案

修改 `.github/workflows/coverage.yml` 的 Configure 步骤，在 `conan install` 之后、
`cmake --preset` 之前，查找 `drogon_ctl` 的绝对路径并通过 `$GITHUB_PATH` 注入到
后续 Build 步骤的 PATH 中：

```yaml
# drogon_ctl lives in the Conan package cache, not in $PATH;
# drogon_create_views() invokes it at build time, so prepend
# its directory for the subsequent cmake --build step.
DROGON_BIN_DIR="$(find ~/.conan2/p -name drogon_ctl -type f -executable -printf '%h\n' | head -1)"
if [ -z "$DROGON_BIN_DIR" ]; then
  echo "::error::drogon_ctl not found in Conan cache"
  exit 1
fi
echo "Found drogon_ctl in $DROGON_BIN_DIR"
echo "$DROGON_BIN_DIR" >> "$GITHUB_PATH"
```

## 状态

- [x] 修复已提交到 coverage.yml
- [ ] 需触发 `gh workflow run coverage.yml --ref fix/issues-batch-1` 验证
  （需另一代理完成构建后 push，避免冲突）
