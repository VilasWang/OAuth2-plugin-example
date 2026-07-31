# OAuth2 Frontend — 设计方案

> 版本: v1.0
> 创建日期: 2026-05-26
> 设计参考: Auth0 Universal Login、Clerk、Supabase Auth UI

---

## 一、项目定位

面向终端用户的 OAuth2/OIDC 客户端 SPA，提供完整的身份认证和账户管理体验。

- **独立项目**：`OAuth2Frontend/`（与 OAuth2Admin 管理后台平级）
- **目标用户**：普通终端用户（非管理员）
- **核心职责**：认证流程 + 账户自助管理 + OAuth2 协议页面

---

## 二、功能模块

### 2.1 认证流程 (Auth)

| 页面 | 路由 | 功能 |
|------|------|------|
| 登录 | `/login` | 用户名密码登录 + MFA 二步验证 |
| 注册 | `/register` | 新用户注册（用户名 + 邮箱 + 密码） |
| 忘记密码 | `/forgot-password` | 发送重置邮件（防枚举设计） |
| 重置密码 | `/reset-password?token=xxx` | Token 验证 + 设置新密码 |
| 邮箱验证 | `/verify-email?token=xxx` | 验证邮箱地址 |

### 2.2 OAuth2 协议页面 (OAuth)

| 页面 | 路由 | 功能 |
|------|------|------|
| 授权回调 | `/callback` | 授权码换 Token |
| 授权同意 | `/consent` | 第三方应用 Scope 授权确认/拒绝 |
| 设备授权 | `/device/verify` | 输入设备码完成授权 (RFC 8628) |

### 2.3 账户管理 (Account)

| 页面 | 路由 | 功能 |
|------|------|------|
| 仪表盘 | `/` | 用户概览（ID、邮箱、角色、快捷入口） |
| 个人资料 | `/profile` | 查看个人信息、重发验证邮件 |
| 安全设置 | `/security` | 修改密码、MFA 启用/禁用 |
| 已授权应用 | `/authorized-apps` | 查看/撤销第三方应用授权 |

---

## 三、技术架构

### 3.1 技术栈

| 类别 | 选择 | 理由 |
|------|------|------|
| 框架 | Vue 3 + TypeScript | 与 Admin 后台一致，团队统一 |
| 构建 | Vite 6 | 快速 HMR，Tree-shaking |
| 状态管理 | Pinia | Vue 3 官方推荐 |
| 样式 | TailwindCSS 4 | 快速开发，一致性好 |
| HTTP | Axios + 拦截器 | Token 自动刷新、错误统一处理 |
| 路由 | Vue Router 4 | 路由守卫、懒加载 |
| 测试 | Playwright | E2E 测试 |

### 3.2 项目结构

```
OAuth2Frontend/
├── src/
│   ├── components/
│   │   ├── ui/              # 基础 UI 组件（Button, Input, Alert）
│   │   └── shared/          # 业务通用组件（Logo）
│   ├── composables/         # 组合式函数
│   ├── layouts/             # 页面布局（AuthLayout, AppLayout）
│   ├── pages/
│   │   ├── auth/            # 认证相关页面
│   │   ├── oauth/           # OAuth2 协议页面
│   │   └── account/         # 账户管理页面
│   ├── router/              # 路由配置 + 守卫
│   ├── services/            # API 调用层（http 客户端 + 业务 service）
│   ├── stores/              # Pinia 状态管理
│   ├── types/               # TypeScript 类型定义
│   ├── App.vue
│   ├── main.ts
│   └── style.css
├── tests/
│   └── e2e/                 # Playwright E2E 测试
│       └── helpers/         # Mock API + 辅助函数
├── playwright.config.ts
├── vite.config.ts
├── tsconfig.json
├── nginx.conf               # 生产部署 Nginx 配置
├── .env.example             # 环境变量模板
└── package.json
```

### 3.3 分层设计

```
┌─────────────────────────────────────────┐
│  Pages (页面组件)                        │
│  - 组合 UI 组件 + 调用 Store/Service     │
├─────────────────────────────────────────┤
│  Stores (Pinia 状态管理)                 │
│  - auth store: 登录状态、Token 管理       │
├─────────────────────────────────────────┤
│  Services (API 调用层)                   │
│  - http.ts: Axios 实例 + 拦截器          │
│  - authService.ts: 认证相关 API          │
│  - userService.ts: 用户自助 API          │
├─────────────────────────────────────────┤
│  Components (可复用组件)                  │
│  - ui/: 无业务逻辑的基础组件             │
│  - shared/: 带业务语义的通用组件          │
└─────────────────────────────────────────┘
```

---

## 四、UI/UX 设计规范

### 4.1 设计参考

- **Auth0 Universal Login**：简洁的认证页面，居中卡片式布局
- **Clerk**：现代化的账户管理面板
- **Supabase Auth UI**：开发者友好的认证组件

### 4.2 视觉规范

| 属性 | 值 |
|------|-----|
| 主色调 | Indigo-600 (#4F46E5) |
| 认证页面 | 渐变背景 + 居中白色卡片 |
| 账户页面 | 顶部导航 + 白色内容区 |
| 圆角 | 8px (卡片)、6px (按钮)、8px (输入框) |
| 字体 | 系统字体栈 (Inter fallback) |
| 响应式 | 移动端优先，断点 sm/md/lg |

### 4.3 布局方案

**认证页面 (AuthLayout)**：
```
┌─────────────────────────────────────┐
│  渐变背景 (indigo-50 → blue-100)    │
│                                     │
│     ┌─────────────────────┐         │
│     │  Logo               │         │
│     │  标题               │         │
│     │  表单               │         │
│     │  操作按钮           │         │
│     │  底部链接           │         │
│     └─────────────────────┘         │
│                                     │
└─────────────────────────────────────┘
```

**账户页面 (AppLayout)**：
```
┌─────────────────────────────────────┐
│  Top Nav: Logo | 导航 | 用户 | 登出  │
├─────────────────────────────────────┤
│                                     │
│  Page Content (max-w-7xl)           │
│                                     │
└─────────────────────────────────────┘
```

---

## 五、安全设计

### 5.1 Token 管理

- Access Token 存储在 `localStorage`（SPA 标准做法）
- Refresh Token 存储在 `localStorage`
- 401 响应自动触发 Token 刷新
- 刷新失败自动跳转登录页

### 5.2 PKCE 支持

- 授权码流程使用 PKCE (RFC 7636)
- `code_verifier` 存储在 `sessionStorage`
- 回调页面完成后立即清除

### 5.3 防护措施

- CSRF：Bearer Token 模式天然防 CSRF
- XSS：Vue 3 默认转义 + 不使用 v-html
- 密码重置：防枚举设计（无论邮箱是否存在都返回成功）
- MFA：TOTP 验证码 6 位数字，限制输入格式

---

## 六、生产化配置

### 6.1 环境变量

```env
VITE_API_BASE_URL=         # API 基础 URL（生产环境为空，使用同域）
VITE_CLIENT_ID=vue-client  # OAuth2 Client ID
VITE_CLIENT_SECRET=        # OAuth2 Client Secret
VITE_REDIRECT_URI=         # OAuth2 回调 URI
VITE_APP_NAME=OAuth2 App   # 应用名称
```

### 6.2 Nginx 部署

- SPA fallback (`try_files $uri /index.html`)
- API 反向代理 (`/api/`, `/oauth2/`, `/.well-known/`)
- 静态资源缓存 (`Cache-Control: public, immutable`)

### 6.3 Docker 集成

- 多阶段构建（Node builder → Nginx runtime）
- 与 `docker-compose.yml` 集成

---

## 七、测试策略

| 层级 | 工具 | 覆盖 |
|------|------|------|
| E2E 测试 | Playwright | 所有用户流程（登录、注册、MFA、密码管理、授权） |
| Mock 模式 | page.route() 拦截 | 无需后端即可运行测试 |

### 关键 E2E 场景

1. 登录成功 → 跳转 Dashboard
2. 登录失败 → 显示错误
3. MFA 验证流程
4. 注册 → 成功提示
5. 忘记密码 → 发送邮件
6. 修改密码
7. MFA 启用/禁用
8. 已授权应用查看/撤销
9. OAuth2 同意页面
10. 设备授权验证
