---
name: full-frontend-test
description: 全量前端测试 — Admin(tsc+build+16 e2e) + User(build+8 e2e+3 property test)
---

# 全量前端测试

执行 Admin 和 User 两个前端的完整测试。E2e 使用 mock API（不需要后端服务器）。

## 前置检查

1. `npm install` 已在两个前端目录执行
2. `npx playwright install`（浏览器已安装）

## Admin 前端（frontends/admin）

```bash
cd frontends/admin
npx tsc --noEmit                      # 1. TypeScript 类型检查
npm run build                         # 2. 构建（tsc && vite build）
npx playwright test --reporter=line   # 3. E2e（16 spec：api-docs/application-detail/applications/auth/dashboard/error-handling/logs/navigation/roles/scopes-management/security/settings/tokens/user-detail/users/ux）
```

## User 前端（frontends/user）

```bash
cd frontends/user
npm run build                         # 1. 构建（vite build）
npx playwright test --reporter=line   # 2. E2e（8 spec：account/auth/navigation/oauth/password-reset/registration-validation/security/session-management）
npm run test:unit                     # 3. 属性测试（3：crossAppConsistency/errorAdapter/messageCatalog）
```

## 通过标准

**所有步骤全部 PASS，0 失败。**
