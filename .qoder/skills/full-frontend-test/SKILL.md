---
name: full-frontend-test
description: 全量前端测试 — Admin(build[tsc&&vite build]+16 e2e) + User(build[vite build]+8 e2e+test:unit 5 文件含 3 属性测试)
---

# 全量前端测试

执行 Admin 和 User 两个前端的完整测试。E2e 使用 mock API（不需要后端服务器）。

## 前置检查

1. `npm install` 已在两个前端目录执行
2. `npx playwright install`（浏览器已安装）

## Admin 前端（frontends/admin）

```bash
cd frontends/admin
npm run build                         # 1. 类型检查 + 构建（脚本即 `tsc && vite build`）
npx playwright test --reporter=line   # 2. E2e（16 spec：api-docs/application-detail/applications/auth/dashboard/error-handling/logs/navigation/roles/scopes-management/security/settings/tokens/user-detail/users/ux）
```

> 注意：admin 的类型检查已内嵌在 `npm run build`（`tsc && vite build`）里，无需单独跑 `npx tsc --noEmit`；且它用的是普通 `tsc`（非 `vue-tsc`），不检查 `.vue` 单文件组件。E2e 通过 Playwright 路由拦截做 Mock API，**不需要后端服务器**。

## User 前端（frontends/user）

```bash
cd frontends/user
npm run build                         # 1. 构建（vite build，无 tsc 类型检查）
npx playwright test --reporter=line   # 2. E2e（8 spec：account/auth/navigation/oauth/password-reset/registration-validation/security/session-management）
npm run test:unit                     # 3. 单元测试（vitest run，共 5 个文件：3 个属性测试 crossAppConsistency/errorAdapter/messageCatalog + tests/unit/pkce.test.ts + tests/unit/smoke.test.ts）
```

> User 的 `build` 脚本是纯 `vite build`（不做类型检查）；`test:unit`（= `vitest run`）实际运行 5 个测试文件，其中 3 个属性测试位于 `src/services/*.property.test.ts`。E2e 同样使用 Mock API，无需后端。

## 通过标准

**所有步骤全部 PASS，0 失败。**
