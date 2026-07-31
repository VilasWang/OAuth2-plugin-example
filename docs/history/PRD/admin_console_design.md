# OAuth2 Admin Console 设计文档

> 版本: v1.0  
> 创建日期: 2026-05-20  
> 设计参考: Auth0 Dashboard (业内最佳 IdP 管理台 UX)  
> 技术栈: Vue 3 + Vite + Pinia + TailwindCSS + Headless UI

---

## 一、项目定位

独立管理后台，供平台超级管理员管理 OAuth2 授权服务器的所有资源。

- **独立项目**：`OAuth2Admin/`（与 OAuth2Frontend 平级）
- **部署方式**：同域名 `/admin` 路径（nginx 反代），后续可扩展到独立域名
- **认证方式**：通过 OAuth2 授权码流程登录（吃自己的狗粮），admin 角色才能访问

---

## 二、页面清单与功能

### 2.1 总览 Dashboard

| 页面 | 路由 | 功能 |
|------|------|------|
| 仪表盘 | `/admin/dashboard` | 关键指标概览：活跃用户数、活跃 token 数、今日登录次数、失败登录次数、系统健康状态 |

### 2.2 应用管理 (Applications)

| 页面 | 路由 | 功能 |
|------|------|------|
| 应用列表 | `/admin/applications` | 所有 OAuth2 Client 列表（名称、类型、创建时间、状态） |
| 应用详情 | `/admin/applications/:id` | 查看/编辑 Client 配置 |
| 创建应用 | `/admin/applications/new` | 注册新 Client（向导式） |

**应用详情 Tab 页**：
- **基本信息**：client_id、name、type (PUBLIC/CONFIDENTIAL)、logo
- **认证配置**：redirect_uris、allowed_grant_types、token_endpoint_auth_method
- **Token 设置**：access_token_ttl、refresh_token_ttl、id_token_ttl
- **Scope 权限**：已分配的 scope 列表（可增删）
- **安全设置**：require_pkce、backchannel_logout_uri
- **凭据管理**：重置 client_secret（确认后显示一次）
- **危险操作**：禁用/删除 Client

### 2.3 用户管理 (Users)

| 页面 | 路由 | 功能 |
|------|------|------|
| 用户列表 | `/admin/users` | 分页列表（用户名、邮箱、状态、MFA、角色、注册时间） |
| 用户详情 | `/admin/users/:id` | 查看/编辑用户信息 |

**用户详情 Tab 页**：
- **基本信息**：username、email、email_verified、created_at
- **安全**：MFA 状态、账号锁定状态、失败登录次数、重置密码
- **角色**：已分配角色（可增删）
- **授权记录**：已授权的 Client 列表（可撤销）
- **登录历史**：最近登录记录（时间、IP、UA、结果）
- **危险操作**：禁用/删除用户

### 2.4 角色与权限 (Roles & Permissions)

| 页面 | 路由 | 功能 |
|------|------|------|
| 角色列表 | `/admin/roles` | 所有角色（名称、描述、用户数） |
| 角色详情 | `/admin/roles/:id` | 编辑角色、管理权限分配 |
| 权限列表 | `/admin/permissions` | 所有权限定义 |

### 2.5 Scope 管理

| 页面 | 路由 | 功能 |
|------|------|------|
| Scope 列表 | `/admin/scopes` | OAuth2 Scope 定义（名称、描述、映射角色、是否默认） |
| 创建/编辑 Scope | `/admin/scopes/:name` | CRUD |

### 2.6 审计日志 (Audit Logs)

| 页面 | 路由 | 功能 |
|------|------|------|
| 日志列表 | `/admin/logs` | 分页、可筛选（时间范围、事件类型、用户、结果） |
| 日志详情 | `/admin/logs/:id` | 完整事件详情（JSON） |

**筛选维度**：
- 时间范围（最近 1h / 24h / 7d / 30d / 自定义）
- 事件类型（login_success, login_failure, token_issued, token_revoked, password_changed, mfa_enabled, client_created, etc.）
- 操作者（用户/客户端）
- 结果（success / failure）

### 2.7 系统设置 (Settings)

| 页面 | 路由 | 功能 |
|------|------|------|
| 通用设置 | `/admin/settings/general` | issuer URL、服务名称、Logo |
| 安全策略 | `/admin/settings/security` | 密码策略、MFA 策略、PKCE 强制、邮箱验证强制 |
| 登录页定制 | `/admin/settings/branding` | 登录页 Logo、颜色、文案 |
| 签名密钥 | `/admin/settings/keys` | OIDC 签名密钥查看、轮转 |

### 2.8 组织管理 (Organizations) — 多租户

| 页面 | 路由 | 功能 |
|------|------|------|
| 组织列表 | `/admin/organizations` | 所有租户（slug、名称、用户数、客户端数） |
| 组织详情 | `/admin/organizations/:slug` | 编辑组织信息、品牌配置 |

---

## 三、页面布局设计

### 3.1 整体布局（Auth0 风格）

```
┌─────────────────────────────────────────────────────────┐
│  Top Bar: Logo | 搜索 | 通知 | 用户头像/下拉            │
├────────────┬────────────────────────────────────────────┤
│            │                                            │
│  Sidebar   │  Main Content Area                        │
│            │                                            │
│  Dashboard │  ┌─ Breadcrumb ─────────────────────┐     │
│  Applicat. │  │ Applications > vue-client         │     │
│  Users     │  └──────────────────────────────────┘     │
│  Roles     │                                            │
│  Scopes    │  ┌─ Page Content ───────────────────┐     │
│  Logs      │  │                                   │     │
│  Settings  │  │  Tab Bar: Info | Auth | Tokens    │     │
│  Orgs      │  │                                   │     │
│            │  │  Form / Table / Cards             │     │
│            │  │                                   │     │
│            │  └───────────────────────────────────┘     │
│            │                                            │
├────────────┴────────────────────────────────────────────┤
│  Footer: Version v5.1.0 | Docs | API Reference          │
└─────────────────────────────────────────────────────────┘
```

### 3.2 设计规范

| 属性 | 值 |
|------|-----|
| 主色调 | Indigo-600 (#4F46E5) |
| 侧边栏 | 深色背景 (Gray-900)，白色文字 |
| 内容区 | 白色背景，Gray-50 页面底色 |
| 字体 | Inter (正文)，JetBrains Mono (代码/ID) |
| 圆角 | 8px (卡片)，6px (按钮)，4px (输入框) |
| 阴影 | sm (卡片)，md (弹窗) |
| 响应式 | 侧边栏 < 1024px 时折叠为图标模式 |

### 3.3 组件库

基于 **Headless UI** + **TailwindCSS** 自建组件：
- DataTable（分页、排序、筛选、批量操作）
- FormBuilder（动态表单、验证）
- Modal / SlideOver（确认弹窗、侧滑面板）
- Toast / Notification（操作反馈）
- CodeBlock（显示 client_id、token 等）
- StatusBadge（在线/离线/锁定/已验证）
- DateRangePicker（审计日志筛选）

---

## 四、交互设计

### 4.1 关键交互流程

**创建应用（向导式）**：
1. 选择应用类型（SPA / Native / Server / M2M）
2. 填写基本信息（名称、Logo、描述）
3. 配置 redirect_uris
4. 选择 grant_types 和 scopes
5. 确认 → 显示 client_id + client_secret（一次性）

**重置 Client Secret**：
1. 点击"重置密钥"
2. 二次确认弹窗（警告：旧密钥立即失效）
3. 确认后显示新密钥（带复制按钮，30 秒后自动隐藏）

**用户角色分配**：
1. 用户详情 → 角色 Tab
2. 点击"添加角色"
3. 多选下拉（搜索 + 勾选）
4. 保存 → Toast 提示成功

**审计日志查看**：
1. 默认显示最近 24h
2. 左侧筛选面板（事件类型多选、时间范围、用户搜索）
3. 点击行展开详情（JSON 格式）
4. 支持导出 CSV

### 4.2 操作反馈

| 操作类型 | 反馈方式 |
|----------|----------|
| 创建成功 | Toast (绿色) + 跳转到详情页 |
| 更新成功 | Toast (绿色) "Changes saved" |
| 删除确认 | Modal (红色按钮) + 输入确认文字 |
| 错误 | Toast (红色) + 错误详情 |
| 加载中 | Skeleton 占位 + Spinner |
| 空状态 | 插图 + 引导文案 + CTA 按钮 |

---

## 五、数据结构与接口

### 5.1 后端 API（已实现）

后台直接调用现有的 Admin API：

| 接口 | 方法 | 用途 |
|------|------|------|
| `/api/admin/clients` | GET | 列表 |
| `/api/admin/clients` | POST | 创建 |
| `/api/admin/clients/:id` | DELETE | 删除 |
| `/api/admin/clients/:id/reset-secret` | POST | 重置密钥 |
| `/api/admin/users` | GET | 用户列表 |
| `/api/admin/users/:id/roles` | PUT | 分配角色 |
| `/api/admin/scopes` | GET | Scope 列表 |
| `/api/admin/organizations` | GET/POST | 组织管理 |
| `/api/admin/organizations/:slug` | GET | 组织详情 |
| `/oauth2/userinfo` | GET | 当前用户信息 |
| `/health/ready` | GET | 系统健康 |

### 5.2 需要新增的 API

所有 API 已实现 ✅

| 接口 | 方法 | 用途 | 状态 |
|------|------|------|------|
| `/api/admin/dashboard/stats` | GET | 仪表盘统计数据 | ✅ |
| `/api/admin/users/:id` | GET | 用户详情 | ✅ |
| `/api/admin/users/:id` | PUT | 更新用户信息 | ✅ |
| `/api/admin/users/:id/enable` | POST | 启用用户 | ✅ |
| `/api/admin/users/:id/roles` | GET | 获取用户角色 | ✅ |
| `/api/admin/roles` | GET/POST | 角色列表/创建 | ✅ |
| `/api/admin/roles/:id` | PUT/DELETE | 角色编辑/删除 | ✅ |
| `/api/admin/scopes` | POST | 创建 Scope | ✅ |
| `/api/admin/scopes/:id` | PUT/DELETE | 更新/删除 Scope | ✅ |

### 5.3 前端状态管理 (Pinia Stores)

```
stores/
├── auth.ts          # 登录状态、token 管理
├── clients.ts       # 应用列表、CRUD
├── users.ts         # 用户列表、详情
├── roles.ts         # 角色权限
├── scopes.ts        # Scope 管理
├── logs.ts          # 审计日志
├── settings.ts      # 系统设置
├── organizations.ts # 组织管理
└── dashboard.ts     # 仪表盘数据
```

---

## 六、安全性设计

### 6.1 认证

- 管理后台通过 OAuth2 授权码流程 + PKCE 登录
- 登录后获取 access_token，存储在内存（不用 localStorage）
- Token 自动刷新（refresh_token 存 httpOnly cookie 或内存）
- 会话超时：30 分钟无操作自动登出

### 6.2 授权

- 所有 `/api/admin/*` 接口要求 `admin` 角色
- 前端路由守卫：无 admin 角色跳转到 403 页面
- 敏感操作（删除、重置密钥）需要二次确认
- 超级管理员操作记录审计日志

### 6.3 前端安全

- CSP 策略：`script-src 'self'`
- XSS 防护：Vue 3 默认转义 + DOMPurify（富文本场景）
- CSRF：OAuth2 Bearer Token 模式天然防 CSRF
- 敏感数据：client_secret 显示后 30 秒自动隐藏，不可复制第二次

### 6.4 部署安全

- 管理后台静态文件由 nginx 托管
- API 请求通过 nginx 反代到后端（同源，无 CORS 问题）
- 生产环境强制 HTTPS
- 可选：IP 白名单限制管理后台访问

---

## 七、性能优化

| 策略 | 实现 |
|------|------|
| 路由懒加载 | 每个页面模块独立 chunk |
| API 缓存 | SWR 模式（先显示缓存，后台刷新） |
| 列表虚拟滚动 | 用户/日志列表 > 100 条时启用 |
| 分页 | 服务端分页（page + per_page） |
| 搜索防抖 | 300ms debounce |
| 图片懒加载 | Logo/头像使用 IntersectionObserver |
| Bundle 优化 | Tree-shaking + gzip + brotli |
| 首屏优化 | 关键 CSS 内联 + preload 字体 |

---

## 八、测试策略

| 层级 | 工具 | 覆盖 |
|------|------|------|
| 单元测试 | Vitest | 组件逻辑、Store、工具函数 |
| 组件测试 | Vue Test Utils + Vitest | 表单验证、交互 |
| E2E 测试 | Playwright | 关键流程（登录、创建应用、用户管理） |
| API Mock | MSW (Mock Service Worker) | 开发时模拟后端 |
| 可访问性 | axe-core | WCAG 2.1 AA |

**关键 E2E 场景**：
1. 管理员登录 → 仪表盘加载
2. 创建应用 → 获取 client_id/secret
3. 用户列表 → 搜索 → 查看详情 → 分配角色
4. 审计日志 → 筛选 → 导出
5. 重置 client_secret → 确认 → 显示新密钥

---

## 九、部署架构

```
                    ┌─────────────────────┐
                    │      Nginx          │
                    │                     │
    /admin/*  ───→  │  静态文件 (Vue SPA) │
    /api/*    ───→  │  反代 → :5555       │
    /oauth2/* ───→  │  反代 → :5555       │
                    │                     │
                    └─────────────────────┘
                              │
                    ┌─────────┴─────────┐
                    │  OAuth2Server      │
                    │  (Drogon :5555)    │
                    └───────────────────┘
```

**Docker 部署**：
```yaml
# docker-compose.yml 新增
oauth2-admin:
  image: nginx:alpine
  volumes:
    - ./OAuth2Admin/dist:/usr/share/nginx/html/admin
    - ./nginx-admin.conf:/etc/nginx/conf.d/default.conf
  ports:
    - "8081:80"
```

**nginx 配置**：
```nginx
location /admin {
    alias /usr/share/nginx/html/admin;
    try_files $uri $uri/ /admin/index.html;
}

location /api/ {
    proxy_pass http://oauth2-backend:5555;
}
```

---

## 十、项目结构

```
OAuth2Admin/
├── public/
│   └── favicon.ico
├── src/
│   ├── assets/           # 静态资源（Logo、图标）
│   ├── components/       # 通用组件
│   │   ├── ui/           # 基础 UI（Button, Input, Modal, Table）
│   │   ├── layout/       # 布局（Sidebar, TopBar, PageHeader）
│   │   └── shared/       # 业务通用（StatusBadge, CodeBlock, DatePicker）
│   ├── composables/      # 组合式函数（useAuth, usePagination, useToast）
│   ├── pages/            # 页面组件
│   │   ├── dashboard/
│   │   ├── applications/
│   │   ├── users/
│   │   ├── roles/
│   │   ├── scopes/
│   │   ├── logs/
│   │   ├── settings/
│   │   └── organizations/
│   ├── router/           # 路由配置 + 守卫
│   ├── stores/           # Pinia 状态管理
│   ├── services/         # API 调用层
│   ├── utils/            # 工具函数
│   ├── types/            # TypeScript 类型定义
│   ├── App.vue
│   └── main.ts
├── tests/
│   ├── unit/
│   ├── components/
│   └── e2e/
├── index.html
├── package.json
├── vite.config.ts
├── tailwind.config.js
├── tsconfig.json
└── README.md
```

---

## 十一、技术栈选型

| 类别 | 选择 | 理由 |
|------|------|------|
| 框架 | Vue 3 + TypeScript | 与现有前端一致，团队熟悉 |
| 构建 | Vite 5 | 快速 HMR，生产构建优化 |
| 状态管理 | Pinia | Vue 3 官方推荐，TypeScript 友好 |
| 样式 | TailwindCSS 3 | 快速开发，一致性好，Auth0 风格易实现 |
| UI 组件 | Headless UI + 自建 | 无样式约束，完全可控 |
| 图表 | Chart.js / ECharts | 仪表盘指标可视化 |
| 表格 | TanStack Table | 高性能虚拟表格 |
| 表单 | VeeValidate + Zod | 类型安全的表单验证 |
| HTTP | Axios + 拦截器 | Token 自动刷新、错误统一处理 |
| 路由 | Vue Router 4 | 路由守卫、懒加载 |
| 测试 | Vitest + Playwright | 单元 + E2E |
| 代码规范 | ESLint + Prettier | 统一风格 |

---

## 十二、实施计划

```text
Phase 1 (1 周): 项目脚手架 + 登录 + 布局 ✅
  ├── Vite + Vue 3 + TailwindCSS 初始化
  ├── OAuth2 登录流程（PKCE）
  ├── 侧边栏 + TopBar + 路由守卫
  └── Dashboard 页面（调用 /health/ready）

Phase 2 (1 周): 应用管理 + 用户管理 ✅
  ├── 应用列表/创建/删除
  ├── 用户列表/角色分配
  └── DataTable 组件

Phase 3 (1 周): Scope/审计 ✅
  ├── Scope 管理
  ├── 审计日志（筛选 + 分页）
  └── 系统设置页面

Phase 4 (3 天): 优化 + 测试 + 部署 ✅
  ├── E2E 测试（Playwright 53 tests）
  ├── Docker 部署配置
  └── 文档

Phase 5 (1 周): 功能补全 ✅
  ├── 5A: 应用详情页 + Scope 分配 ✅
  ├── 5B: Token 管理（列表/撤销/筛选）✅
  ├── 5C: OIDC 签名密钥查看 ✅
  ├── 5D: E2E 测试补充 ✅
  └── 5E: 用户详情/角色管理/Scope管理 ✅

Phase 6 (完成): 全功能补全
  ├── 用户详情页（Info/Security/Roles Tab）✅
  ├── 角色管理页（列表/创建/编辑/删除）✅
  ├── Scope 管理页（列表/创建/编辑/删除）✅
  ├── Dashboard 统计数据 ✅
  ├── 后端 API 测试（32 tests）✅
  ├── 前端 E2E 测试（123 tests）✅
  └── OpenAPI 文档补全 ✅
```

---

## 十三、文档规划

| 文档 | 内容 |
|------|------|
| README.md | 快速开始、开发指南、部署说明 |
| ARCHITECTURE.md | 前端架构、目录结构、设计决策 |
| API.md | 后端 API 对接说明（指向 openapi.yaml） |
| DEPLOYMENT.md | Docker/nginx 部署配置 |
| CONTRIBUTING.md | 开发规范、PR 流程 |
