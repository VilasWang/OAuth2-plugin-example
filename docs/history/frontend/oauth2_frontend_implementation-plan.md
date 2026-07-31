# OAuth2 Frontend — 实施计划

> 创建日期: 2026-05-26

---

## 实施阶段

### Phase 1: 项目脚手架 + 核心架构 ✅

- [x] Vite + Vue 3 + TypeScript + TailwindCSS 4 初始化
- [x] 项目结构搭建（pages/services/stores/components/types）
- [x] HTTP 客户端封装（Axios + Token 拦截器 + 自动刷新）
- [x] Pinia Auth Store（login/logout/refresh/MFA）
- [x] Vue Router 配置（路由守卫、懒加载）
- [x] 基础 UI 组件库（AppButton, AppInput, AppAlert）
- [x] 双布局系统（AuthLayout + AppLayout）
- [x] 环境变量配置（.env.example）
- [x] Playwright 配置

### Phase 2: 认证页面 ✅

- [x] LoginPage（用户名密码 + MFA 二步验证）
- [x] RegisterPage（用户名 + 邮箱 + 密码 + 确认密码）
- [x] ForgotPasswordPage（邮箱输入 + 防枚举）
- [x] ResetPasswordPage（Token 验证 + 新密码设置）
- [x] VerifyEmailPage（Token 验证结果展示）

### Phase 3: OAuth2 协议页面 ✅

- [x] CallbackPage（授权码换 Token）
- [x] ConsentPage（Scope 授权确认/拒绝）
- [x] DeviceVerifyPage（设备码输入验证）

### Phase 4: 账户管理页面 ✅

- [x] DashboardPage（用户概览 + 快捷入口）
- [x] ProfilePage（个人信息 + 邮箱验证状态）
- [x] SecurityPage（修改密码 + MFA 管理）
- [x] AuthorizedAppsPage（已授权应用列表 + 撤销）

### Phase 5: E2E 测试 ← 当前阶段

- [ ] Mock API 层（helpers/mock-api.ts）
- [ ] 认证流程测试（login, register, forgot-password）
- [ ] MFA 流程测试
- [ ] 账户管理测试（profile, security, authorized-apps）
- [ ] OAuth2 协议页面测试（consent, device-verify）
- [ ] 导航和路由守卫测试

### Phase 6: 生产化完善

- [x] Nginx 配置
- [x] Docker 集成（Dockerfile 多阶段构建）
- [ ] 性能优化（路由懒加载已完成，图片优化待定）
- [x] 设计方案文档
- [x] 实施计划文档

---

## 当前进度

| 阶段 | 状态 | 说明 |
|------|------|------|
| Phase 1 | ✅ 完成 | 项目架构搭建完毕 |
| Phase 2 | ✅ 完成 | 5 个认证页面 |
| Phase 3 | ✅ 完成 | 3 个 OAuth2 协议页面 |
| Phase 4 | ✅ 完成 | 4 个账户管理页面 |
| Phase 5 | 🔄 进行中 | E2E 测试待编写 |
| Phase 6 | ✅ 大部分完成 | 文档和配置 |

---

## 文件清单

### 已完成文件

```
src/
├── components/ui/AppButton.vue          ✅
├── components/ui/AppInput.vue           ✅
├── components/ui/AppAlert.vue           ✅
├── components/shared/AppLogo.vue        ✅
├── layouts/AuthLayout.vue               ✅
├── layouts/AppLayout.vue                ✅
├── pages/auth/LoginPage.vue             ✅
├── pages/auth/RegisterPage.vue          ✅
├── pages/auth/ForgotPasswordPage.vue    ✅
├── pages/auth/ResetPasswordPage.vue     ✅
├── pages/auth/VerifyEmailPage.vue       ✅
├── pages/oauth/CallbackPage.vue         ✅
├── pages/oauth/ConsentPage.vue          ✅
├── pages/oauth/DeviceVerifyPage.vue     ✅
├── pages/account/DashboardPage.vue      ✅
├── pages/account/ProfilePage.vue        ✅
├── pages/account/SecurityPage.vue       ✅
├── pages/account/AuthorizedAppsPage.vue ✅
├── router/index.ts                      ✅
├── stores/auth.ts                       ✅
├── services/http.ts                     ✅
├── services/authService.ts              ✅
├── services/userService.ts              ✅
├── types/index.ts                       ✅
├── App.vue                              ✅
├── main.ts                              ✅
└── style.css                            ✅

配置文件:
├── package.json                         ✅
├── vite.config.ts                       ✅
├── tsconfig.json                        ✅
├── playwright.config.ts                 ✅
├── nginx.conf                           ✅
├── .env.example                         ✅
├── .gitignore                           ✅
└── index.html                           ✅
```

### 待完成文件

```
tests/e2e/
├── helpers/mock-api.ts                  ⏳
├── auth.spec.ts                         ⏳
├── account.spec.ts                      ⏳
├── oauth.spec.ts                        ⏳
└── navigation.spec.ts                   ⏳
```
