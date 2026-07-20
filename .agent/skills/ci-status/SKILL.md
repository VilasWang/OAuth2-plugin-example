---
name: ci-status
description: 检查 OAuth2 项目在三个平台（Linux、Windows、macOS）上的 CI/CD 状态
---

# CI/CD 状态检查技能

快速检查所有平台的 CI 状态，无需打开 GitHub。

## 使用时机

当代码变更后需要验证 CI 是否通过，或快速定位哪个平台出现问题时使用 `/ci-status`。

## 检查的平台

| 平台 | 工作流文件 | 状态检查命令 |
|------|-----------|-------------|
| Linux | `.github/workflows/ci-linux.yml` | `gh run list --workflow=ci-linux.yml --limit 1` |
| Windows | `.github/workflows/ci-windows.yml` | `gh run list --workflow=ci-windows.yml --limit 1` |
| macOS | `./github/workflows/ci-macos.yml` | `gh run list --workflow=ci-macos.yml --limit 1` |

## 主要功能

### 快速状态检查
```bash
# 检查所有平台的最新运行状态
gh run list --workflow=ci-linux.yml --limit 1
gh run list --workflow=ci-windows.yml --limit 1
gh run list --workflow=ci-macos.yml --limit 1
```

### 详细状态查看
```bash
# 查看具体运行详情
gh run view [run-id]

# 查看运行日志
gh run view [run-id] --log

# 查看失败的工作流
gh run list --json --jq '.[] | select(.status != "success")'
```

### 重新触发失败的 CI
```bash
# 重新触发特定工作流
gh workflow run ci-linux.yml
gh workflow run ci-windows.yml
gh workflow run ci-macos.yml
```

## 状态说明

| 状态 | 含义 | 行动 |
|------|------|------|
| success | 构建成功，测试通过 | 无需操作 |
| failure | 构建失败或测试失败 | 查看日志并修复 |
| pending | 等待运行中 | 等待完成 |
| cancelled | 用户取消 | 重新触发 |

## 输出格式

```markdown
## CI/CD 状态报告

### ✅ Linux CI
- **状态**: [成功/失败/进行中]
- **运行ID**: [run-id]
- **提交**: [commit hash]
- **时间**: [运行时间]
- **详情**: `gh run view [run-id]`

### ✅ Windows CI
- **状态**: [状态信息]
- **运行ID**: [run-id]
- **提交**: [commit hash]
- **时间**: [运行时间]

### ✅ macOS CI
- **状态**: [状态信息]
- **运行ID**: [run-id]
- **提交**: [commit hash]
- **时间**: [运行时间]
```

## 快捷命令别名

建议在 `.bashrc` 或 `.bash_profile` 中添加别名：

```bash
alias ci-check='gh run list --workflow=ci-linux.yml --limit 3 && gh run list --workflow=ci-windows.yml --limit 3 && gh run list --workflow=ci-macos.yml --limit 3'
alias ci-failed='gh run list --json --jq ".[] | select(.status != \"success\") | .html_url"'
alias ci-retry='gh workflow run ci-linux.yml && gh workflow run ci-windows.yml && gh workflow run ci-macos.yml'
```

## 前置条件

- 需要安装 GitHub CLI (`gh`)
- 需要进行 GitHub 认证 (`gh auth login`)
- 需要有仓库的访问权限
