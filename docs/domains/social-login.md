# 社交登录指南

后端社交登录按"提供方适配器"模式实现；**GitHub 是当前唯一前后端完整接线的提供方**，
Google 与微信为"后端就绪、前端自接"模式（后端路由与配置已就绪，前端按钮需自行接入）。

## GitHub（已完整接线，主线索）

- 后端路由：`POST /api/github/login`；前端 `frontends/user` 已有"Sign in with GitHub"按钮
  （OAuth App 的 client id 经 `VITE_GITHUB_CLIENT_ID` 注入）。
- 账号模型：`oauth2_subject_mappings(provider, subject)` 映射到本地用户；首次登录按
  `createLinkedUser` 语义创建带默认 `user` 角色的本地账号（用户名冲突时拒绝采用，fail-closed）。

## Google（后端就绪）

1. Google Cloud 侧创建 OAuth 2.0 客户端（Web 应用，回调填 `https://<your-host>/api/google/login`）。
2. 后端配置（`config.json`）：

```json
"external_auth": {
    "google": {
        "client_id": "<your-client-id>",
        "client_secret": "<your-client-secret>"
    }
}
```

3. 后端路由 `POST /api/google/login` 接收 `{ code, redirect_uri }` 并完成 code 交换。
4. **前端按钮需自行接线**（当前 UI 未内置 Google 按钮——验证时用 curl 直接调后端路由）。

## 微信（后端就绪；要求公网回调域名）

1. 微信开放平台创建网站应用（**不支持 localhost 回调**，需备案域名）。
2. 后端配置同上结构（`external_auth.wechat`：appid / app_secret）。
3. 后端路由 `POST /api/wechat/login`。
4. 本地开发三招：hosts 把回调域名指到 127.0.0.1 / Nginx 反代 / 内网穿透。

## 通用安全注意

- 社交回调的 `state` 必须校验（防 CSRF）；提供方返回的 subject 只信任服务端 code 交换结果，
  绝不信任前端提交的用户信息。
- 解绑社交账号有"最后一种登录方式"保护与并发解绑竞态的已知限制（见 `docs/history`
  归档的 social-link 设计与 CHANGELOG #54/#69 修复记录）。

> 合并自已退役的 google-guide.md 与 wechat-guide.md（docs 治理 A2），并修正了 google 指南
> 中"前端无按钮却让用户点击按钮"的自相矛盾。
