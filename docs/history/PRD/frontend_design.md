# OAuth2 Frontend SPA 设计文档

> 版本: v1.0
> 设计参考: Auth0 Universal Login + Clerk.dev + Supabase Auth UI
> 技术栈: Vue 3 + TypeScript + Pinia + TailwindCSS 4 + Vite 6

---

## 一、设计目标

构建一个**生产级用户端 SPA**，覆盖 OAuth2 系统中除 Admin Console 外的所有用户交互场景。

**设计原则：**
- 品牌化：统一的视觉语言，不是"测试页面"
- 信任感：认证页面需要传递安全和专业感
- 可访问性：WCAG 2.1 AA 标准
- 响应式：移动端优先设计
- 国际化就绪：文案集中管理

---

## 二、项目结构

```
OAuth2Frontend/
├── public/
│   └── favicon.svg
├── src/
│   ├── assets/              # 静态资源（Logo、图标、插图）
│   ├── components/          # 可复用 UI 组件
│   │   ├── ui/              # 基础组件（Button, Input, Card, Alert, Badge）
│   │   └── shared/          # 业务通用（PasswordStrength, OtpInput, AppLogo）
│   ├── composables/         # 组合式函数
│   │   ├── useToast.ts      # Toast 通知
│   │   └── useApi.ts        # API 调用封装
│   ├── layouts/             # 页面布局
│   │   ├── AuthLayout.vue   # 认证页面布局（居中卡片 + 品牌背景）
│   │   └── AppLayout.vue    # 已登录页面布局（顶部导航 + 内容区）
│   ├── pages/               # 页面组件
│   │   ├── auth/            # 认证相关
│   │   │   ├── LoginPage.vue
│   │   │   ├── RegisterPage.vue
│   │   │   ├── ForgotPasswordPage.vue
│   │   │   ├── ResetPasswordPage.vue
│   │   │   ├── VerifyEmailPage.vue
│   │   │   └── MfaChallengePage.vue
│   │   ├── oauth/           # OAuth2 协议页面
│   │   │   ├── CallbackPage.vue
│   │   │   ├── ConsentPage.vue
│   │   │   └── DeviceVerifyPage.vue
│   │   └── account/         # 已登录用户页面
│   │       ├── DashboardPage.vue
│   │       ├── ProfilePage.vue
│   │       ├── SecurityPage.vue
│   │       └── AuthorizedAppsPage.vue
│   ├── router/              # 路由配置
│   │   └── index.ts
│   ├── services/            # API 服务层
│   │   ├── authService.ts   # 认证相关 API
│   │   ├── userService.ts   # 用户自助 API
│   │   └── http.ts          # Axios 实例 + 拦截器
│   ├── stores/              # Pinia 状态管理
│   │   └── auth.ts
│   ├── types/               # TypeScript 类型定义
│   │   └── index.ts
│   ├── App.vue
│   ├── main.ts
│   └── style.css
├── tests/
│   └── e2e/
│       ├── helpers/
│       │   └── mock-api.ts
│       ├── auth.spec.ts
│       ├── account.spec.ts
│       └── oauth.spec.ts
├── .env.example             # 环境变量模板
├── .eslintrc.cjs            # ESLint 配置
├── .prettierrc              # Prettier 配置
├── index.html
├── nginx.conf               # 生产部署 Nginx 配置
├── package.json
├── playwright.config.ts     # E2E 测试配置
├── tsconfig.json
└── vite.config.ts
```

---

## 三、页面设计

### 3.1 认证页面布局（AuthLayout）

参考 Auth0 Universal Login 的设计：
- 左侧/背景：品牌渐变色 + 产品 Slogan
- 右侧/中央：白色卡片，包含表单
- 底部：版权信息 + 隐私政策链接

### 3.2 已登录页面布局（AppLayout）

参考 GitHub Settings / Clerk Dashboard：
- 顶部导航栏：Logo + 导航链接 + 用户头像下拉
- 内容区：左侧侧边导航（桌面端）+ 主内容

### 3.3 设计规范

| 属性 | 值 |
|------|-----|
| 主色调 | Indigo-600 (#4F46E5) |
| 成功色 | Emerald-500 |
| 警告色 | Amber-500 |
| 错误色 | Rose-500 |
| 字体 | Inter (正文)，JetBrains Mono (代码) |
| 圆角 | 12px (卡片)，8px (按钮)，6px (输入框) |
| 阴影 | xl (认证卡片)，sm (内容卡片) |

---

## 四、功能场景覆盖

| 场景 | 页面 | 对应后端 API |
|------|------|------|
| 登录 | LoginPage | POST /oauth2/login |
| MFA 验证 | MfaChallengePage | POST /oauth2/mfa/verify |
| 注册 | RegisterPage | POST /api/register |
| 忘记密码 | ForgotPasswordPage | POST /api/password-reset/request |
| 重置密码 | ResetPasswordPage | POST /api/password-reset/confirm |
| 邮箱验证 | VerifyEmailPage | GET /api/verify-email |
| OAuth2 回调 | CallbackPage | POST /oauth2/token |
| 授权同意 | ConsentPage | POST /oauth2/consent |
| 设备授权 | DeviceVerifyPage | POST /oauth2/device/verify |
| 仪表盘 | DashboardPage | GET /oauth2/userinfo |
| 个人资料 | ProfilePage | GET /api/me |
| 安全设置 | SecurityPage | PUT /api/me/password, POST /api/me/mfa/* |
| 已授权应用 | AuthorizedAppsPage | GET/DELETE /api/me/authorized-apps |

---

## 五、测试策略

### E2E 测试（Playwright）

| 测试文件 | 覆盖场景 |
|----------|----------|
| auth.spec.ts | 登录、注册、忘记密码、MFA |
| account.spec.ts | 个人资料、安全设置、已授权应用 |
| oauth.spec.ts | 回调处理、授权同意、设备验证 |

### 测试方式

使用 Playwright `page.route()` 拦截 API 请求（与 OAuth2Admin 相同的 Mock 模式），无需真实后端。

---

## 六、生产化配置

- `.env.example` — 环境变量模板
- `nginx.conf` — 生产 Nginx 配置（SPA fallback + API proxy + 缓存策略）
- `Dockerfile` — 多阶段构建（build → nginx）
- CSP 头配置
- Gzip/Brotli 压缩
- 静态资源长期缓存（content hash）
