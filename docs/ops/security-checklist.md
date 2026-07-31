# Security Checklist - Hardcoding Fixes

本文档提供了硬编码修复后的安全检查清单，确保敏感信息不会被泄露到版本控制系统。

## ✅ 已实施的安全措施

### 1. Git Ignore 配置

[.gitignore](../../.gitignore) 已正确配置以下规则：

```gitignore
# Sensitive Configuration Files
.env
.env.local
.env.*.local
*.pem
*.key
*.cert
*.crt
secrets/
credentials/

# Frontend runtime config with production credentials
frontends/user/public/config.json
```

### 2. 安全文件结构

```
frontends/user/
├── .env                    # ❌ 被忽略 - 包含实际凭证
├── .env.example            # ✅ 可提交 - 仅包含示例
├── public/
│   ├── config.json         # ❌ 被忽略 - 包含实际凭证
│   └── config.example.json # ✅ 可提交 - 仅包含示例
└── src/
    └── config/
        └── auth.config.js  # ✅ 可提交 - 不含敏感信息
```

## 🔒 安全检查清单

### 开发环境配置

- [ ] **从不提交** `.env` 文件到版本控制
- [ ] **从不提交** `frontends/user/public/config.json` 到版本控制
- [ ] **仅提交** `.env.example` 和 `config.example.json` 作为模板
- [ ] 确保 `.env.example` 和 `config.example.json` 仅包含占位符，不含真实凭证

### 生产环境配置

- [ ] 使用环境变量管理生产凭证（推荐）
- [ ] 或使用 CI/CD 平台的密钥管理功能
- [ ] 或使用配置管理工具（如 Vault、AWS Secrets Manager）
- [ ] 定期轮换密钥和客户端凭证

### 验证命令

在提交代码前，运行以下命令确保没有敏感文件被跟踪：

```bash
# 检查是否有 .env 文件被跟踪
git ls-files | grep "\.env$"

# 检查是否有前端 config.json 被跟踪
git ls-files | grep "frontends/user/public/config.json"

# 检查当前 git 状态
git status

# 查看差异中的敏感信息
git diff --no-ext-diff | grep -i "secret\|password\|token\|api_key\|appid"
```

## 🚨 紧急处理：如果敏感信息已被提交

### 立即执行

```bash
# 1. 从 git 历史中移除敏感文件
git filter-branch --force --index-filter \
  "git rm --cached --ignore-unmatch frontends/user/.env" \
  --prune-empty --tag-name-filter cat -- --all

# 2. 从 git 历史中移除前端配置
git filter-branch --force --index-filter \
  "git rm --cached --ignore-unmatch frontends/user/public/config.json" \
  --prune-empty --tag-name-filter cat -- --all

# 3. 强制推送到远程（⚠️ 谨慎操作）
git push origin --force --all
git push origin --force --tags

# 4. 清理本地引用
git for-each-ref --format="delete %(refname)" refs/original | git update-ref --stdin
git reflog expire --expire=now --all
git gc --prune=now --aggressive
```

### 后续措施

1. **立即轮换所有已泄露的密钥和凭证**
2. 通知所有开发者重新克隆仓库
3. 启用分支保护规则，要求代码审查
4. 配置 pre-commit hooks 防止未来意外提交

## 🛡️ Pre-commit Hook 配置

创建 `.git/hooks/pre-commit` 文件（可执行权限）：

```bash
#!/bin/bash
# Pre-commit hook to prevent sensitive files from being committed

# 检查是否有 .env 文件被提交
if git diff --cached --name-only | grep -E "\.env$|frontends/user/public/config\.json"; then
    echo "❌ 错误: 尝试提交敏感配置文件！"
    echo "   请检查 .gitignore 配置"
    exit 1
fi

# 检查是否有硬编码的密钥模式
if git diff --cached | grep -E "^\+.*[\"']([A-Za-z0-9_-]{32,}|wx[a-zA-Z0-9]{16,}|sk-[a-zA-Z0-9]{32,})[\"']"; then
    echo "⚠️  警告: 可能包含硬编码的密钥或凭证"
    echo "   请使用环境变量或配置文件"
    read -p "   是否继续提交? (y/N) " -n 1 -r
    echo
    if [[ ! $REPLY =~ ^[Yy]$ ]]; then
        exit 1
    fi
fi

exit 0
```

使用方法：
```bash
# 赋予执行权限
chmod +x .git/hooks/pre-commit
```

## 📋 部署前安全检查

### 前端部署检查

```bash
# 1. 验证没有使用示例配置
grep -r "YOUR_" frontends/user/dist/
grep -r "your-domain.com" frontends/user/dist/

# 2. 验证环境变量已设置
env | grep VITE_

# 3. 验证生产构建不包含开发配置
cat frontends/user/dist/config.json 2>/dev/null || echo "✅ config.json 不存在于构建中"
```

### 后端部署检查

```bash
# 1. 验证配置文件权限
ls -la apps/server/config/config*.json

# 2. 验证日志目录权限
ls -la apps/server/logs/

# 3. 验证数据库密码不为空
grep "passwd" apps/server/config/config.json | grep '""'
```

## 🔄 密钥轮换计划

### 定期轮换

- **访问令牌**: 每 1-3 小时（自动过期）
- **刷新令牌**: 每 30 天（自动过期）
- **客户端密钥**: 每 90 天
- **管理员密码**: 每 60 天
- **API 密钥**: 每 30 天

### 紧急轮换

在以下情况下立即轮换所有密钥：
- 发现代码泄露
- 离职员工访问
- 可疑的访问日志
- 安全审计发现漏洞

## 📞 安全事件响应

如果发现安全漏洞：

1. **立即**: 撤销所有可能泄露的密钥
2. **1小时内**: 通知安全团队和项目负责人
3. **24小时内**: 完成安全评估和影响分析
4. **7天内**: 完成所有密钥轮换和漏洞修复
5. **30天内**: 完成全面安全审计

---

**文档版本**: v1.0
**最后更新**: 2026-05-02
**维护者**: OAuth2 Plugin 开发团队

**紧急联系**: 如发现安全问题，请立即通知项目负责人。
