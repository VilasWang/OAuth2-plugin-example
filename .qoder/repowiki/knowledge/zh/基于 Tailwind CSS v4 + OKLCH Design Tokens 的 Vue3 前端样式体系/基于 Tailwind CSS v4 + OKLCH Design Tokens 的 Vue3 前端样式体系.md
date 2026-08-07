---
kind: frontend_style
name: 基于 Tailwind CSS v4 + OKLCH Design Tokens 的 Vue3 前端样式体系
category: frontend_style
scope:
    - '**'
source_files:
    - frontends/admin/src/styles/design-tokens.css
    - frontends/user/src/styles/design-tokens.css
    - frontends/admin/src/style.css
    - frontends/user/src/style.css
    - frontends/admin/vite.config.ts
    - frontends/user/vite.config.ts
    - frontends/admin/package.json
    - frontends/user/package.json
    - frontends/admin/src/components/ui/AppButton.vue
    - frontends/user/src/components/ui/AppInput.vue
---

## 1. 系统/技术栈

AuthForge 的前端包含两个独立的 Vue 3 应用：`frontends/admin`（管理员控制台）与 `frontends/user`（用户门户）。两者共享同一套视觉规范，但各自维护独立的构建产物。

- **框架**：Vue 3 (`vue@^3.5`) + TypeScript + Vite 6
- **样式引擎**：Tailwind CSS v4（通过 `@tailwindcss/vite` 插件集成），采用新的 `@import "tailwindcss"` 导入方式，不再依赖 `tailwind.config.js`
- **设计令牌**：自研 OKLCH 色彩系统的 CSS Custom Properties 文件 `src/styles/design-tokens.css`，在两个应用中完全一致
- **UI 原子组件**：每个应用内自建 `components/ui/App*` 系列组件（Button、Input、Card、Modal、Table、Alert、Badge、Skeleton、Select、EmptyState），使用 Tailwind 原子类组合实现，不引入第三方 UI 库
- **无头 UI 库**：admin 应用额外引入 `@headlessui/vue` 与 `@heroicons/vue`，用于可访问性原语与图标
- **状态管理**：Pinia（admin 用 v3，user 用 v2）
- **路由**：Vue Router 4

## 2. 关键文件

| 文件 | 作用 |
|---|---|
| `frontends/admin/src/styles/design-tokens.css` | 全局设计令牌定义（颜色、排版、间距、阴影、动效、z-index、布局常量、暗色主题覆盖、基础 reset） |
| `frontends/user/src/styles/design-tokens.css` | 与 admin 完全一致的 tokens 副本，保证双应用视觉统一 |
| `frontends/admin/src/style.css` / `frontends/user/src/style.css` | 入口样式，仅做 `@import design-tokens.css` 和 `@import tailwindcss` |
| `frontends/admin/vite.config.ts` / `frontends/user/vite.config.ts` | Vite 配置，启用 `@tailwindcss/vite` 插件，设置开发代理与 base path |
| `frontends/admin/package.json` / `frontends/user/package.json` | 依赖声明，确认 Tailwind v4、Headless UI、Hero Icons 等 |
| `frontends/admin/src/components/ui/*.vue` | 原子 UI 组件（AppButton、AppInput、AppCard、AppModal、AppTable 等） |
| `frontends/user/src/components/ui/*.vue` | 用户门户精简版原子组件（AppButton、AppInput、AppAlert） |

## 3. 架构与设计约定

### 设计令牌分层
`design-tokens.css` 将样式变量分为四层：
1. **原始色板 (Primitive)**：基于 OKLCH 的 `--color-primary-*`、`--color-neutral-*`、`--color-success-*`、`--color-warning-*`、`--color-error-*`、`--color-info-*`，强调感知均匀的色彩空间
2. **语义色 (Semantic)**：`--color-brand`、`--color-text-*`、`--color-bg-*`、`--color-border-*`、`--color-success/warning/error/info` 等，从原始色派生
3. **设计常量**：字体族 (`--font-sans`、`--font-mono`)、字号阶梯 (modular scale 1.125)、字重、行高、字距、8px 节奏的 `--space-*`、圆角 `--radius-*`、阴影层级、过渡时长与缓动函数、z-index 标尺、布局常量（侧边栏宽度、头部高度、内容最大宽度、认证卡片宽度）、无障碍目标尺寸
4. **暗色主题覆盖**：通过 `[data-theme="dark"]` 选择器仅覆写语义层，原始色保持不变，实现主题切换

### 样式组织模式
- 所有组件使用 **Tailwind 原子类** 直接书写样式，不在组件内编写 `<style>` 块
- 组件通过 props 暴露变体（如 `variant: 'primary'|'secondary'|'danger'|'ghost'`、`size: 'sm'|'md'|'lg'`），内部以条件 class 组合实现不同外观
- 颜色值在组件中直接使用 Tailwind 预设色名（如 `bg-sky-700`、`text-rose-600`、`border-neutral-300`），而非引用 CSS 自定义属性；tokens 主要作为全局基线被 `body`、`:focus-visible`、滚动条等全局样式消费
- 两个应用的 `design-tokens.css` 完全相同，确保跨应用一致性

### 响应式策略
- 未使用媒体查询断点，而是依赖 Tailwind 的响应式前缀（如 `sm:`、`md:`、`lg:`）进行响应式布局
- 通过 `prefers-reduced-motion` 媒体查询提供无障碍动画降级
- 布局常量集中在 tokens 中（`--sidebar-width`、`--header-height`、`--content-max-width`），便于集中调整

### 可访问性
- 统一的 `:focus-visible` 样式，使用 `--focus-ring` 令牌定义聚焦环
- 输入组件内置 `aria-invalid`、`aria-describedby`、`role="alert"` 等无障碍属性
- 遵循 44px 最小点击目标尺寸 (`--target-size-min`)

## 4. 约定与约束

- **禁止硬编码颜色**：颜色必须来自 Tailwind 预设或 `design-tokens.css` 中的 CSS 变量，不得出现任意十六进制色值
- **间距遵循 8px 节奏**：使用 `--space-*` 或对应的 Tailwind spacing 工具类，避免随意像素值
- **主题切换机制**：通过给根元素设置 `data-theme="dark"` 切换暗色模式，新增主题时需同时覆写语义层 token
- **双应用同步**：`admin` 与 `user` 的 `design-tokens.css` 必须保持完全一致，任何视觉变更需同步到两处
- **组件样式风格**：原子组件全部使用 Tailwind 原子类组合，props 控制变体与尺寸，不扩展 CSS 模块或 SCSS
- **构建产物隔离**：admin 应用 base 路径为 `/admin/`，user 应用使用默认路径，二者独立构建、独立部署
- **无全局 CSS 污染**：`style.css` 仅导入 tokens 与 Tailwind，组件样式完全内聚于 `.vue` 文件的模板 class 中