---
name: release
description: 自动化 OAuth2 项目发布流程，包括版本号更新、标签创建和 changelog 生成
disable-model-invocation: true
---

# Release 管理技能

自动化 OAuth2 项目的发布流程，确保版本号一致性、标签正确创建和变更日志生成。

## 使用时机

当代码准备好发布时使用 `/release`。

## 发布流程

### 1. 版本号确认
检查当前版本号：
- `OAuth2Server/CMakeLists.txt` 中的项目版本
- `OAuth2Plugin/CMakeLists.txt` 中的插件版本
- 确保版本号格式一致（语义化版本：MAJOR.MINOR.PATCH）

### 2. 版本号更新
更新所有相关文件中的版本号：
- `CMakeLists.txt` 文件
- `package.json` 文件（如果有前端）
- 文档中的版本引用
- `.version` 文件（如果存在）

### 3. 创建 Git 标签
```bash
# 创建带注释的标签
git tag -a v[version] -m "Release version [version]"

# 推送标签到远程
git push origin v[version]
```

### 4. 生成 Changelog
```bash
# 生成自上次标签以来的变更
git log --pretty=format:"- %s" $(git describe --tags --abbrev=0 HEAD^)..HEAD > CHANGELOG.md

# 或者使用 git-changelog 工具（如果安装）
git changelog -o CHANGELOG.md
```

### 5. 创建 GitHub Release
```bash
# 使用 GitHub CLI 创建 release
gh release create v[version] \
  --title "OAuth2 Server v[version]" \
  --notes "Release notes for version [version]" \
  --target [commit-hash]
```

## 版本号规范

遵循语义化版本规范（Semantic Versioning）：

- **MAJOR** (主版本号)：不兼容的 API 变更
- **MINOR** (次版本号)：向下兼容的功能性新增
- **PATCH** (修订号)：向下兼容的问题修正

示例：
- `1.0.0` → `1.1.0` （新增功能，向后兼容）
- `1.1.0` → `2.0.0` （破坏性变更）
- `1.1.0` → `1.1.1` （bug 修复）

## Pre-release 版本

对于预发布版本，使用以下后缀：
- `1.0.0-alpha.1` （Alpha 版本）
- `1.0.0-beta.1` （Beta 版本）
- `1.0.0-rc.1` （Release Candidate）

## 发布检查清单

在发布前验证：

- [ ] 所有测试通过（三个平台的 CI 都成功）
- [ ] 版本号已更新到所有相关文件
- [ ] Changelog 已更新
- [ ] 文档已更新（API 文档等）
- [ ] 安全扫描已通过（无已知漏洞）
- [ ] 性能测试符合预期
- [ ] 向后兼容性已验证

## 回滚计划

如果发布后发现问题：

```bash
# 删除远程标签
git push origin :refs/tags/v[version]

# 删除本地标签
git tag -d v[version]

# 创建 hotfix 分支
git checkout -b hotfix/[issue-description]

# 修复后发布新版本
```

## 发布通知

发布后通知相关方：

- 开发团队：内部通知
- 用户：发布公告
- 文档：更新安装和升级指南

## 文件位置

需要更新版本号的文件：
- `OAuth2Server/CMakeLists.txt`
- `OAuth2Plugin/CMakeLists.txt`
- `OAuth2Admin/package.json`（如果有前端变更）
- `README.md`（版本引用）
- `CHANGELOG.md`（变更日志）
