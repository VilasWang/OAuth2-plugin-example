# 生产化部署指南

本指南说明如何将 OAuth2 全栈系统（用户前端 + 管理后台 + 后端 API）部署到生产环境。

---

## 架构概览

```
                    Internet
                       │
                ┌──────┴──────┐
                │   Nginx     │  :80 → :443 (TLS)
                │  (反向代理)  │
                └──────┬──────┘
          ┌────────────┼────────────┐
          │            │            │
    ┌─────┴─────┐ ┌────┴────┐ ┌────┴────┐
    │ Frontend  │ │  Admin  │ │ Backend │
    │ (Vue SPA) │ │ (Vue)   │ │ (C++)   │
    │  :80      │ │  :80    │ │  :5555  │
    └───────────┘ └─────────┘ └────┬────┘
                                   │
                         ┌─────────┼─────────┐
                         │                   │
                   ┌─────┴─────┐     ┌───────┴───────┐
                   │ PostgreSQL│     │     Redis     │
                   │   :5432   │     │    :6379      │
                   └───────────┘     └───────────────┘
```

**路由规则（Nginx）**：
- `/api/*`, `/oauth2/*`, `/.well-known/*`, `/health` → Backend
- `/admin/*` → Admin Console
- `/*` (其他) → User Frontend

---

## 前置条件

### 硬件要求
- **CPU**: 2 核心以上
- **内存**: 4GB 以上（推荐 8GB）
- **磁盘**: 20GB 以上可用空间
- **网络**: 公网 IP，域名已解析到服务器

### 操作系统支持

- Ubuntu 20.04 / 22.04 / 24.04 LTS
- Debian 11 / 12
- CentOS Stream 8 / 9
- Rocky Linux 8 / 9

### 软件依赖安装

#### 1. 安装 Docker

**Ubuntu/Debian**:
```bash
# 更新包索引
sudo apt update

# 安装必要依赖
sudo apt install -y ca-certificates curl gnupg lsb-release

# 添加 Docker 官方 GPG 密钥
sudo mkdir -p /etc/apt/keyrings
curl -fsSL https://download.docker.com/linux/ubuntu/gpg | sudo gpg --dearmor -o /etc/apt/keyrings/docker.gpg

# 设置 Docker 仓库
echo \
  "deb [arch=$(dpkg --print-architecture) signed-by=/etc/apt/keyrings/docker.gpg] https://download.docker.com/linux/ubuntu \
  $(lsb_release -cs) stable" | sudo tee /etc/apt/sources.list.d/docker.list > /dev/null

# 安装 Docker Engine
sudo apt update
sudo apt install -y docker-ce docker-ce-cli containerd.io docker-buildx-plugin docker-compose-plugin

# 启动 Docker 服务
sudo systemctl start docker
sudo systemctl enable docker

# 验证安装
docker --version
docker compose version
```

**CentOS/Rocky Linux**:
```bash
# 安装必要依赖
sudo yum install -y yum-utils device-mapper-persistent-data lvm2

# 添加 Docker 仓库
sudo yum-config-manager --add-repo https://download.docker.com/linux/centos/docker-ce.repo

# 安装 Docker
sudo yum install -y docker-ce docker-ce-cli containerd.io docker-buildx-plugin docker-compose-plugin

# 启动 Docker 服务
sudo systemctl start docker
sudo systemctl enable docker

# 验证安装
docker --version
docker compose version
```

#### 2. 配置 Docker 用户组（可选但推荐）

```bash
# 创建 docker 组（如果不存在）
sudo groupadd docker

# 将当前用户添加到 docker 组
sudo usermod -aG docker $USER

# 重新登录或运行以下命令使组权限生效
newgrp docker

# 验证：无需 sudo 运行 docker
docker ps
```

#### 2.5. 配置 Docker 镜像加速器（中国大陆必需）

由于 Docker Hub 在中国大陆访问不稳定，拉取镜像会超时（典型错误：`dial tcp registry-1.docker.io:443: i/o timeout`），必须配置镜像加速器。

以下加速器地址经实测（2026-06）在阿里云服务器上验证可用：

**创建或修改 Docker 配置文件**:

```bash
sudo mkdir -p /etc/docker
sudo tee /etc/docker/daemon.json > /dev/null << 'EOF'
{
  "registry-mirrors": [
    "https://docker.1panel.live",
    "https://docker.awsl9527.cn",
    "https://docker.xuanyuan.me"
  ],
  "log-driver": "json-file",
  "log-opts": {
    "max-size": "100m",
    "max-file": "3"
  }
}
EOF
```

> 说明：配置多个加速器，Docker 会按顺序尝试，任一可用即拉取成功。

**重启 Docker 服务使配置生效**:

```bash
sudo systemctl daemon-reload
sudo systemctl restart docker
sudo systemctl status docker
```

**验证镜像加速器配置**:

```bash
# 检查配置是否被加载（应显示上述 registry-mirrors 列表）
docker info | grep -A 5 "Registry Mirrors"

# 测试拉取镜像（本项目需要的全部镜像）
docker pull postgres:15-alpine
docker pull redis:7-alpine
docker pull nginx:stable-alpine
docker pull prom/prometheus:latest
docker pull ubuntu:22.04
```

如果某个加速器报错（如 `502` 或 `i/o timeout`），Docker 会自动尝试下一个；若全部失败，参考下方故障排除。

**故障排除**:

1. **所有加速器均失败**：访问 [dongyubin/DockerHub](https://github.com/dongyubin/DockerHub) 获取最新可用列表，替换 `daemon.json` 中的地址后重启 Docker。

2. **使用阿里云专属加速器**（需要阿里云账号，最稳定）:
   - 登录 [阿里云容器镜像服务](https://cr.console.aliyun.com/) → 镜像工具 → 镜像加速器
   - 获取专属加速地址（形如 `https://<your_code>.mirror.aliyuncs.com`）
   - 将该地址置于 `daemon.json` 的 `registry-mirrors` 数组首位

3. **使用代理拉取**（如果有可用的代理服务器）:

   ```bash
   # 为 Docker 守护进程配置代理
   sudo mkdir -p /etc/systemd/system/docker.service.d
   sudo tee /etc/systemd/system/docker.service.d/http-proxy.conf > /dev/null << EOF
   [Service]
   Environment="HTTP_PROXY=http://your-proxy:port"
   Environment="HTTPS_PROXY=http://your-proxy:port"
   Environment="NO_PROXY=localhost,127.0.0.1"
   EOF

   sudo systemctl daemon-reload
   sudo systemctl restart docker
   ```

#### 3. 安装 Git

**Ubuntu/Debian**:
```bash
sudo apt install -y git
```

**CentOS/Rocky Linux**:
```bash
sudo yum install -y git
```

#### 4. 安装 OpenSSL（用于生成密钥）

**Ubuntu/Debian**:
```bash
sudo apt install -y openssl
```

**CentOS/Rocky Linux**:
```bash
sudo yum install -y openssl
```

#### 5. 安装 Certbot（用于获取 Let's Encrypt 证书）

**Ubuntu/Debian**:
```bash
sudo apt install -y certbot
```

**CentOS/Rocky Linux**:
```bash
sudo yum install -y certbot
```

### 验证依赖安装

```bash
# 检查 Docker 版本（要求 24+）
docker --version

# 检查 Docker Compose 版本（要求 v2）
docker compose version

# 检查 Git
git --version

# 检查 OpenSSL
openssl version

# 检查 Certbot
certbot --version
```

### 域名和 DNS 配置

1. **域名解析**：确保您的域名（如 `vilas-api.cn`）的 A 记录指向服务器公网 IP
2. **DNS 传播验证**：
   ```bash
   # 检查域名是否正确解析
   dig +short vilas-api.cn
   nslookup vilas-api.cn
   ```
3. **防火墙配置**：确保以下端口可访问：
   - `80/tcp` (HTTP)
   - `443/tcp` (HTTPS)

### 防火墙配置

**Ubuntu (UFW)**:
```bash
sudo ufw allow 80/tcp
sudo ufw allow 443/tcp
sudo ufw enable
```

**CentOS/Rocky Linux (firewalld)**:
```bash
sudo firewall-cmd --permanent --add-service=http
sudo firewall-cmd --permanent --add-service=https
sudo firewall-cmd --reload
```

---

## 快速部署（5 步）

### 1. 克隆项目

```bash
git clone <repo-url>
cd authforge
```

### 2. 生成密钥

```bash
# 生成 JWT 签名密钥
chmod +x scripts/generate-jwt-keys.sh
./scripts/generate-jwt-keys.sh

# 生成 TLS 证书（开发用自签名，生产用 Let's Encrypt）
chmod +x scripts/generate-certs.sh
./scripts/generate-certs.sh
```

**生产环境使用 Let's Encrypt**：
```bash
# 安装 certbot
sudo apt install certbot

# 创建 SSL 证书目录
mkdir -p deploy/nginx/ssl/

# 获取证书（先停止 nginx）
sudo certbot certonly --standalone -d your-domain.com

# 复制证书
cp /etc/letsencrypt/live/your-domain.com/fullchain.pem deploy/nginx/ssl/
cp /etc/letsencrypt/live/your-domain.com/privkey.pem deploy/nginx/ssl/
```

### 3. 配置环境变量

```bash
# 检查模板文件是否存在
[ -f deploy/env/docker.env.example ] && echo "模板文件存在" || echo "错误：模板文件不存在"

cp deploy/env/docker.env.example .env.docker
```

编辑 `.env.docker`，设置强密码与 HTTPS 域名相关配置：

```env
# 运行模式（生产强制校验 HTTPS issuer / 强密码；须配合 OAUTH2_ISSUER=https://）
OAUTH2_ENV=production
OAUTH2_ISSUER=https://your-domain.com

# JWT 签名密钥（生产必填；不设则每次重启 token 失效）
OAUTH2_JWT_KEY_PATH=/app/keys/signing.pem

POSTGRES_USER=oauth2_user
POSTGRES_PASSWORD=<生成强密码>
POSTGRES_DB=oauth2_db

REDIS_PASSWORD=<生成强密码>

OAUTH2_DB_HOST=oauth2-postgres
OAUTH2_DB_PORT=5432
OAUTH2_DB_NAME=oauth2_db
OAUTH2_DB_USER=oauth2_user
OAUTH2_DB_PASSWORD=<与 POSTGRES_PASSWORD 相同>
OAUTH2_REDIS_HOST=oauth2-redis
OAUTH2_REDIS_PORT=6379
OAUTH2_REDIS_PASSWORD=<与 REDIS_PASSWORD 相同>

# CORS / OAuth 回调（HTTPS 域名必填，否则浏览器请求被拦截）
OAUTH2_FRONTEND_URL=https://your-domain.com
OAUTH2_CORS_ALLOW_ORIGINS=https://your-domain.com
OAUTH2_VUE_REDIRECT_URI=https://your-domain.com/callback
OAUTH2_VUE_CLIENT_SECRET=<生成强密码>
OAUTH2_GOOGLE_REDIRECT_URI=https://your-domain.com/callback

# 错误详细度（生产建议 false，不暴露字段级校验错误）
DETAILED_VALIDATION_ERRORS=false

# 邮件服务（SMTP）— 生产环境必须配置
OAUTH2_SMTP_HOST=smtp.example.com
OAUTH2_SMTP_PORT=465
OAUTH2_SMTP_USER=noreply@example.com
OAUTH2_SMTP_PASSWORD=<SMTP 授权码，非邮箱登录密码>
OAUTH2_SMTP_FROM_NAME=OAuth2 Platform
OAUTH2_SMTP_SSL=true

# 前端构建变量（Vite 构建期注入）
# VITE_API_BASE_URL 生产必须留空 → SPA 走相对路径（nginx 同源反代）
VITE_API_BASE_URL=
VITE_CLIENT_ID=vue-client
VITE_REDIRECT_URI=https://your-domain.com/callback
VITE_GITHUB_CLIENT_ID=

DOMAIN=your-domain.com
```

> **重要耦合**：`OAUTH2_ENV=production` 与 `OAUTH2_ISSUER=https://...` 必须同时设置。仅设 production 而不配 HTTPS issuer 会导致后端启动校验失败（`ConfigManager` 的 prod-mode 校验拒绝非 https issuer）。同理 DB/Redis 密码不能是默认的 `123456` / `password`，否则 prod 校验也会拒绝启动。

生成强密码：
```bash
openssl rand -base64 32
```

#### 邮件服务（SMTP）配置说明

后端邮件服务有两种模式（由 `getEmailService()` 根据环境变量自动选择）：

| 模式 | 触发条件 | 行为 |
|------|---------|------|
| **Console 模式** | 未设置 `OAUTH2_SMTP_HOST` / `USER` / `PASSWORD` | 邮件内容只输出到后端日志，**不真正发送** |
| **SMTP 模式** | 上述三个变量均已设置且非空 | 通过 SMTP 真正发送邮件 |

> **生产环境必须配置 SMTP**，否则邮箱验证、密码重置等功能的邮件不会真正发送给用户（只在服务器日志里）。

**常见邮箱服务商配置参考**：

| 服务商 | SMTP 主机 | 端口 | SSL | 凭据说明 |
|--------|----------|------|-----|---------|
| 163 邮箱 | `smtp.163.com` | 465 | true | 授权码（非登录密码） |
| QQ 邮箱 | `smtp.qq.com` | 465 | true | 授权码 |
| Gmail | `smtp.gmail.com` | 465 | true | 应用专用密码（需开两步验证） |
| 腾讯企业邮 | `smtp.exmail.qq.com` | 465 | true | 邮箱密码 |
| 阿里云企业邮 | `smtp.qiye.aliyun.com` | 465 | true | 邮箱密码 |
| SendGrid | `smtp.sendgrid.net` | 587 | false | 用户名 `apikey`，密码为 API Key |

**获取授权码（以 163 为例）**：
1. 登录 163 邮箱网页版
2. 设置 → POP3/SMTP/IMAP
3. 开启 SMTP 服务
4. 按提示生成授权码（16 位字符串）

配置完成后重启后端生效：

```bash
docker compose -f deploy/docker/docker-compose.prod.yml --env-file .env.docker up -d oauth2-backend

# 验证已切换到 SMTP 模式（应输出 "Email service: SMTP (...)"）
docker compose -f deploy/docker/docker-compose.prod.yml logs oauth2-backend | grep "Email service"
```

### 4. 启动服务

```bash
docker compose -f deploy/docker/docker-compose.prod.yml --env-file .env.docker up -d
```

### 5. 验证部署

```bash
# 检查所有容器状态
docker compose -f deploy/docker/docker-compose.prod.yml ps

# 检查后端健康
curl -k https://localhost/health

# 检查前端
curl -k https://localhost/

# 检查管理后台
curl -k https://localhost/admin/
```

---

## 服务详情

### 用户前端 (OAuth2Frontend)

| 项目 | 值 |
|------|-----|
| 容器名 | oauth2-frontend |
| 构建 | Dockerfile (target: frontend-runtime) |
| 基础镜像 | nginx:stable-alpine |
| 内部端口 | 80 |
| 访问路径 | `https://your-domain.com/` |
| 功能 | 登录、注册、个人资料、安全设置、OAuth2 授权 |

### 管理后台 (OAuth2Admin)

| 项目 | 值 |
|------|-----|
| 容器名 | oauth2-admin |
| 构建 | frontends/admin/Dockerfile |
| 基础镜像 | nginx:alpine |
| 内部端口 | 80 |
| 访问路径 | `https://your-domain.com/admin/` |
| 功能 | 应用管理、用户管理、角色/Scope/Token 管理 |

### 后端 API (authforge-server)

| 项目 | 值 |
|------|-----|
| 容器名 | oauth2-backend |
| 构建 | Dockerfile (target: backend-runtime) |
| 基础镜像 | ubuntu:22.04 (minimal) |
| 内部端口 | 5555 |
| 访问路径 | `https://your-domain.com/api/*`, `/oauth2/*` |
| 数据库迁移 | 启动时自动执行（OAUTH2_AUTO_MIGRATE=true） |

### 基础设施

| 服务 | 镜像 | 用途 |
|------|------|------|
| oauth2-postgres | postgres:15-alpine | 主数据库 |
| oauth2-redis | redis:7-alpine | Token 缓存 |
| oauth2-nginx | nginx:stable-alpine | TLS 终止 + 反向代理 |
| oauth2-prometheus | prom/prometheus | 监控指标采集 |

---

## 配置说明

### 后端配置 (config.prod.json)

后端通过环境变量覆盖配置文件中的值（优先级：`.env` 文件 > 系统环境变量 > `config.prod.json` 默认值）：

| 环境变量 | 用途 | 默认值 |
|----------|------|--------|
| `OAUTH2_ENV` | 运行模式（`production` 启用 HTTPS issuer + 强密码严格校验） | development |
| `OAUTH2_ISSUER` | JWT issuer（生产必须 `https://`） | http://localhost:5555 |
| `OAUTH2_JWT_KEY_PATH` | JWT 签名密钥文件路径 | /app/keys/signing.pem |
| `OAUTH2_SIGNING_KEY` | JWT 密钥 PEM 内容（与 `JWT_KEY_PATH` 二选一） | (可选) |
| `OAUTH2_DB_HOST` | PostgreSQL 主机 | postgres |
| `OAUTH2_DB_PORT` | PostgreSQL 端口 | 5432 |
| `OAUTH2_DB_NAME` | 数据库名 | oauth2_db_prod |
| `OAUTH2_DB_USER` | 数据库用户 | oauth2_user |
| `OAUTH2_DB_PASSWORD` | 数据库密码 | (必须设置) |
| `OAUTH2_REDIS_HOST` | Redis 主机 | redis |
| `OAUTH2_REDIS_PORT` | Redis 端口 | 6379 |
| `OAUTH2_REDIS_PASSWORD` | Redis 密码 | (必须设置) |
| `OAUTH2_LISTEN_PORT` | 后端监听端口 | 5555 |
| `OAUTH2_FRONTEND_URL` | 前端 URL（用于重定向等） | http://localhost:5173 |
| `OAUTH2_CORS_ALLOW_ORIGINS` | CORS 允许的源（逗号分隔，覆盖 JSON 数组） | config 中的 localhost 列表 |
| `OAUTH2_VUE_REDIRECT_URI` | vue-client OAuth 回调 URI | config 中的 localhost 值 |
| `OAUTH2_GOOGLE_REDIRECT_URI` | Google OAuth 回调 URI | config 中的 localhost 值 |
| `OAUTH2_VUE_CLIENT_SECRET` | vue-client 密钥 | 123456 |
| `OAUTH2_AUTO_MIGRATE` | 自动执行数据库迁移 | true |
| `DETAILED_VALIDATION_ERRORS` | 是否返回字段级校验错误（生产建议 false） | false |
| `OAUTH2_GITHUB_CLIENT_ID` / `OAUTH2_GITHUB_CLIENT_SECRET` | GitHub OAuth（可选） | (空) |
| `OAUTH2_GOOGLE_CLIENT_ID` / `OAUTH2_GOOGLE_CLIENT_SECRET` | Google OAuth（可选） | (空) |
| `OAUTH2_SMTP_HOST` | SMTP 服务器主机（未设置则邮件走 Console 模式） | (可选) |
| `OAUTH2_SMTP_PORT` | SMTP 端口 | 465 |
| `OAUTH2_SMTP_USER` | SMTP 用户名（完整邮箱地址） | (可选) |
| `OAUTH2_SMTP_PASSWORD` | SMTP 授权码（非邮箱登录密码） | (可选) |
| `OAUTH2_SMTP_FROM_NAME` | 发件人显示名称 | OAuth2 Platform |
| `OAUTH2_SMTP_SSL` | 是否启用 SSL | true |

> **邮件模式说明**：仅当 `OAUTH2_SMTP_HOST` + `OAUTH2_SMTP_USER` + `OAUTH2_SMTP_PASSWORD` 三项均非空时启用真实 SMTP 发送；否则邮件只输出到后端日志。详见上文"邮件服务（SMTP）配置说明"。
>
> **CORS 数组覆盖**：`OAUTH2_CORS_ALLOW_ORIGINS` 是逗号分隔的字符串（如 `https://a.com,https://b.com`），后端启动时自动分割成 JSON 数组覆盖 `config.prod.json` 的 `custom_config.cors.allow_origins`。CORS 校验代码要求该字段是数组，因此**必须**用逗号分隔形式，不要写成 JSON 数组字面量。

### Nginx 配置

`deploy/nginx/nginx.conf` 包含：
- HTTP → HTTPS 自动重定向
- TLS 1.2/1.3 配置
- 限流规则（登录 5次/分钟/IP，API 30次/秒/IP）
- `/metrics` 端点限制内网访问
- HSTS 头

### 前端配置

前端（用户端 OAuth2Frontend）通过 Vite 环境变量配置，**在镜像构建时注入**到 SPA bundle（不是运行时读取）。`docker-compose.prod.yml` 的 `oauth2-frontend.build.args` 从 `.env.docker` 透传这些变量，`Dockerfile` 的 `frontend-builder` 阶段用 `ARG`/`ENV` 暴露给 Vite。

| 变量 | 用途 | 生产值 |
|------|------|--------|
| `VITE_API_BASE_URL` | API 基础 URL | **(空)** — SPA 同域走相对路径，填值会破坏 nginx 反代路由 |
| `VITE_CLIENT_ID` | OAuth2 Client ID | vue-client |
| `VITE_REDIRECT_URI` | OAuth2 回调 URI | https://your-domain.com/callback |
| `VITE_GITHUB_CLIENT_ID` | GitHub "Sign in with GitHub" 按钮（可选） | (空则不显示按钮) |

> **管理后台（OAuth2Admin）无需配置**：源码不读取任何 `import.meta.env`，所有 API 调用走相对路径 `/api/admin/*`，由 nginx 反代到后端。改域名时只需保证 nginx `/admin/` 路由正确，无需重建 admin 镜像。
>
> **改域名需重建前端镜像**：由于 VITE 变量在构建期固化，更换域名后必须 `docker compose ... up -d --build oauth2-frontend`（管理后台不受影响）。

---

## 数据库初始化

首次部署时，后端会自动执行数据库迁移（`OAUTH2_AUTO_MIGRATE=true`）。

如需手动初始化：

```bash
# 进入 postgres 容器
docker exec -it oauth2-postgres psql -U oauth2_user -d oauth2_db

# 或从宿主机执行迁移
docker exec -it oauth2-postgres sh -c '
  for f in /docker-entrypoint-initdb.d/migrations/V*.sql; do
    psql -U oauth2_user -d oauth2_db -f "$f"
  done
'
```

### 创建管理员账号

首次部署后，执行 seed 脚本创建默认管理员：

```bash
# 验证 seed 文件存在
ls apps/server/seed/dev_*.sql || echo "错误：Seed 文件缺失，请检查项目结构"

# 创建管理员账号
docker exec -i oauth2-postgres psql -U oauth2_user -d oauth2_db < apps/server/seed/dev_admin_user.sql
docker exec -i oauth2-postgres psql -U oauth2_user -d oauth2_db < apps/server/seed/dev_admin_console_client.sql
docker exec -i oauth2-postgres psql -U oauth2_user -d oauth2_db < apps/server/seed/dev_vue_client.sql
```

**重要**：生产环境部署后立即修改 admin 密码！

---

## 运维操作

### 查看日志

```bash
# 所有服务
docker compose -f deploy/docker/docker-compose.prod.yml logs -f

# 单个服务
docker compose -f deploy/docker/docker-compose.prod.yml logs -f oauth2-backend
docker compose -f deploy/docker/docker-compose.prod.yml logs -f nginx
```

### 重启服务

```bash
# 重启单个服务
docker compose -f deploy/docker/docker-compose.prod.yml restart oauth2-backend

# 重建并重启（代码更新后）
docker compose -f deploy/docker/docker-compose.prod.yml up -d --build oauth2-backend
docker compose -f deploy/docker/docker-compose.prod.yml up -d --build oauth2-frontend
docker compose -f deploy/docker/docker-compose.prod.yml up -d --build oauth2-admin
```

### 更新部署

```bash
git pull
docker compose -f deploy/docker/docker-compose.prod.yml up -d --build
```

### 数据库备份

```bash
# 备份
docker exec oauth2-postgres pg_dump -U oauth2_user oauth2_db > backup_$(date +%Y%m%d).sql

# 恢复
docker exec -i oauth2-postgres psql -U oauth2_user -d oauth2_db < backup_20260526.sql
```

### 监控

- Prometheus: `http://your-server:9090`
- 后端指标: `https://your-domain.com/metrics`（仅内网可访问）
- 健康检查: `https://your-domain.com/health`

---

## 故障排除

### 容器启动失败

```bash
# 查看容器状态
docker compose -f deploy/docker/docker-compose.prod.yml ps

# 查看失败容器日志
docker compose -f deploy/docker/docker-compose.prod.yml logs oauth2-backend
```

### 数据库连接失败

```bash
# 检查 postgres 是否就绪
docker exec oauth2-postgres pg_isready -U oauth2_user

# 检查网络连通性
docker exec oauth2-backend curl -s http://oauth2-postgres:5432 || echo "Cannot reach postgres"
```

### 证书问题

```bash
# 检查证书是否存在
ls -la deploy/nginx/ssl/

# 检查证书有效期
openssl x509 -in deploy/nginx/ssl/fullchain.pem -noout -dates
```

### 前端 404

如果前端页面刷新后 404，检查 nginx.conf 中的 SPA fallback 配置：
- OAuth2Frontend: `try_files $uri $uri/ /index.html`
- OAuth2Admin: `try_files $uri $uri/ /admin/index.html`

---

## 安全清单

- [ ] 所有密码使用强随机值（`openssl rand -base64 32`）
- [ ] TLS 证书有效且自动续期
- [ ] `.env.docker` 文件权限设为 600
- [ ] `deploy/keys/signing.pem` 权限设为 600
- [ ] 首次部署后修改 admin 默认密码
- [ ] Prometheus 端口 9090 不对外暴露（或加认证）
- [ ] 定期备份数据库
- [ ] 监控磁盘空间（日志、数据库）
