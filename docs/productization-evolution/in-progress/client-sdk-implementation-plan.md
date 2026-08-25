# 客户端 SDK 实施计划（M1 Python + M2 Go + M3 接线 + M4 文档）

> **日期**: 2026-08-18
> **上游设计**: [client-sdk-facility-design.md](client-sdk-facility-design.md) §五–§七（Layer 2）+ §十一（M1/M2 立项修订）
> **上游任务**: [next-phase-implementation-plan.md](../next-phase-implementation-plan.md) C1
> **基线**: master `9c13b41`（v1.2.0，M0 spec 治理已合并）
> **范围**: M1+M2 完整交付、M3 流水线 wiring（实际发布需 secret，见 §W5）、M4 文档与示例。分支 `feat/client-sdk-python-go`。

---

## 一、交付物总览

| # | 交付物 | 类型 | milestone |
|---|--------|------|-----------|
| W1 | `clients/python/`：生成客户端 + 手写 auth 层（m2m/oauth/PKCE）+ 单测 + 示例 | 代码 | M1+M4 |
| W2 | `clients/go/`：生成客户端 + 手写 auth 层（clientcredentials/authcode/PKCE）+ 单测 | 代码 | M2 |
| W3 | `tools/clients/regen_clients.py`（pin 版本、`--check` 漂移门、版本联动检查）+ `.github/workflows/clients-sdk.yml` | 工具+CI | M1/M2 的 D9 |
| W4 | 集成测试（env 门控）：client_credentials → introspect → discovery → 负例 | 测试 | M1/M2 的 AC5 |
| W5 | `release.yml` 新增 `sdk-python` job（PyPI 发布，secret 门控）；Go proxy 发布说明 | CI | M3 wiring |
| W6 | 文档同步：设计文档 §十一（已入）、README（双语）、next-phase 计划、progress-status | 文档 | M4 |

**明确不做**（非目标，承设计 N1–N5 + §十一.6）：
- 不改 C++ 源码、不改 `apps/server/openapi.yaml`（M0 已就绪，本任务纯消费方）
- 不实际发布 PyPI（需 `PYPI_API_TOKEN` secret + PyPI 项目注册，一次性人工前置）
- 不做 TypeScript 客户端、不生成 token 生命周期全自动（D5：auth 手写）
- 不新增 manage.ps1/manage.sh 命令（parity 面不扩大，§十一.5）

---

## 二、W1 — Python 客户端（M1）

### 2.1 目录与文件

```
clients/python/
├── pyproject.toml                    # hatchling；name=fulla-oauth2；version=1.2.0（联动 cmake，CI 校验）
├── openapi-python-client.yaml        # 生成器配置：package_name_override: fulla
├── README.md                         # 安装、quickstart（M2M + authcode）、版本策略、与 C++ SDK 的关系
├── examples/client_credentials_demo.py
├── src/fulla/
│   ├── __init__.py                   # 出口：m2m_client / AsyncM2MClient / OAuthAuthorizationCode / __version__
│   ├── m2m.py                        # ClientCredentialsAuth(httpx.Auth)（同步）
│   │                                 # AsyncClientCredentialsAuth（asyncio 版）
│   │                                 # m2m_client(...) -> AuthenticatedClient（注入完成）
│   │                                 # async_m2m_client(...) -> AsyncAuthenticatedClient
│   ├── oauth.py                      # AuthorizationCodeFlow：build_authorize_url(PKCE S256 可选)、
│   │                                 # exchange_code()、refresh()；PkcePair 生成（secrets+hashlib）
│   └── generated/                    # 156 文件，DO NOT EDIT（regen 脚本产出）
└── tests/
    ├── conftest.py                   # MockTransport fixture
    ├── test_m2m_auth.py              # §2.3 单测矩阵
    ├── test_oauth_pkce.py            # PKCE S256 已知向量（RFC 7636 appendix B）
    └── integration/
        └── test_against_server.py    # FULLA_BASE_URL 门控（默认跳过）
```

### 2.2 关键实现约束

1. **`ClientCredentialsAuth`**（同步）：持 `token_url`、`client_id/secret`、`scopes`、独立 `httpx.Client`（无 auth，防递归）；`auth_flow(request)`：
   - 有效缓存 token（`expires_at - 30s > now`）→ 注入 `Authorization: Bearer`；
   - 401 响应 → 强制刷新一次并重试（仅一次，防循环）；
   - 刷新持 `threading.Lock`（double-check）；token 请求用 HTTP Basic（F-017 client_secret_basic）；
   - token 端点非 200 → 抛 `FullaAuthError`（带 RFC 6749 error/error_description）。
2. **`AsyncClientCredentialsAuth`**：同逻辑，`async def auth_flow` + `asyncio.Lock`；token 请求复用注入目标之外的 `httpx.AsyncClient`。
3. **`m2m_client(base_url, client_id, client_secret, scopes=(), ...)`**：构造 `httpx.Client(base_url, auth=...)` → `AuthenticatedClient(base_url, token="")` → `set_httpx_client()` 注入 → 返回。async 版对称。`token=""` 仅满足构造签名，静态 header 路径被注入绕开（§十一.4）。
4. **401 重试**：重发前 `request.read()`/（async）`aread()` 已定界请求体（当前生成客户端均为 json/form，构造期已序列化，此处是防御）；P4 用例含 POST-with-body 的 401 重试。
5. **`oauth.py`**：不做浏览器自动化；`build_authorize_url()` 生成 `/oauth2/authorize?...`（response_type=code、PKCE challenge、state 建议参数）；`exchange_code(client, code, verifier, redirect_uri)` 调生成的 `post_oauth2_token.sync`；`refresh(client, refresh_token)` 同理（confidential 客户端经 Basic——F-017 适用于**所有** confidential token 请求，body 形态被拒）。
6. **pyproject**：`requires-python >=3.11`；依赖镜像生成声明（`httpx>=0.23.1,<0.29.0`、`attrs>=22.2.0`）；dev 依赖 `pytest`、`pytest-asyncio`（async 单测）。license MIT（与仓库一致）。

### 2.3 单测矩阵（CI 可跑，MockTransport）

| # | 用例 | 断言 |
|---|------|------|
| P1 | token 获取 | Basic 头形态（base64(id:secret)）、grant_type=client_credentials、scope 空格拼接 |
| P2 | Bearer 注入 + 缓存 | 第二个请求不发 token 端点请求 |
| P3 | 提前刷新 | expires_at 过 30s 余量 → 主动刷新 |
| P4 | 401 重试一次 | API 401 → 刷新 → 重试成功（含 POST-with-body 用例，证明重发可用）；刷新后仍 401 → 透传 401 |
| P5 | 错误传播 | token 端点 400 invalid_client → FullaAuthError 带 error 字段 |
| P6 | PKCE S256 | RFC 7636 附录 B 向量（verifier→challenge） |
| P7 | authorize URL | 参数齐全（challenge、state、response_type） |
| P8 | async 版 P1–P4 等价 | asyncio 下同矩阵 |

### 2.4 验收标准

- [x] `pytest clients/python/tests`（除 integration）全绿
- [x] `pip install -e clients/python` 后 `from fulla import m2m_client` 可用；`import fulla.generated` 完整
- [x] `python -m build clients/python` 产出 sdist+wheel（发布就绪证据）
- [x] 集成测试（§四）对本地全栈绿

---

## 三、W2 — Go 客户端（M2）

### 3.1 目录与文件

```
clients/go/
├── go.mod                     # module github.com/voidvec/fulla/clients/go；go 1.24
├── go.sum
├── oapi-codegen.yaml          # 生成配置：package generated；generate: models+client；pin v2.8.0（注释）
├── generated/client.gen.go    # DO NOT EDIT（14471 行量级）
├── auth/
│   ├── clientcredentials.go   # NewM2MClient(ctx, baseurl, id, secret, scopes...) ->
│   │                          #   *generated.Client（x/oauth2 clientcredentials + WithHTTPClient）
│   ├── authcode.go            # BuildAuthorizeURL / ExchangeCode / Refresh（x/oauth2 v2.Endpoint + PKCE）
│   ├── pkce.go                # CreateVerifier/CreateChallenge（crypto/rand + sha256）
│   └── errors.go              # AuthError（包装 token 端点 RFC 6749 错误）
├── auth_test.go               # httptest 单测（§3.3 矩阵）
├── integration_test.go        # build tag `integration` + env 门控
└── README.md
```

### 3.2 关键实现约束

1. `clientcredentials.Config{ClientID, ClientSecret, TokenURL: base+"/oauth2/token", Scopes, AuthStyle: oauth2.AuthStyleInHeader}`（F-017：body 形态被拒）；`cfg.Client(ctx)` 产出的 `*http.Client` 经 `generated.NewClient(base, generated.WithHTTPClient(hc))` 注入。base URL 规范化（尾斜杠）。
2. `authcode`：`oauth2.Endpoint{AuthURL: base+"/oauth2/authorize", TokenURL: base+"/oauth2/token"}` 的 `AuthCodeURL` + PKCE 参数；Exchange/Refresh 直接构 `url.Values` 调 token 端点（与生成客户端解耦，auth 层只依赖稳定请求形态，承设计 §九 风险表）；**confidential 客户端一律 HTTP Basic**（F-017 适用于所有 confidential token 请求，body 形态被拒）——G8 断言 exchange 请求带 Basic 头。
3. `x/oauth2` 版本 pin 进 go.mod（`golang.org/x/oauth2 v0.x`）。
4. 单测不依赖网络：`httptest.NewServer` 模拟 token 端点与 API 端点。

### 3.3 单测矩阵

| # | 用例 | 断言 |
|---|------|------|
| G1 | token 获取 | 请求头 Basic、form 字段 grant_type/scope |
| G2 | TokenSource 缓存复用 | token 端点仅命中一次（并发下亦然） |
| G3 | 过期自动刷新 | 短 expires_in 后第二次 API 调用触发新 token |
| G4 | token 端点错误传播 | 401 invalid_client → 包装错误含 error 字段 |
| G5 | PKCE S256 向量 | RFC 7636 附录 B |
| G6 | BuildAuthorizeURL | 参数齐全 |
| G7 | gofmt + go vet | 零告警 |
| G8 | authcode exchange 用 Basic | exchange 请求头 Authorization: Basic（F-017） |

### 3.4 验收标准

- [x] `go build ./... && go vet ./...` 零输出；`gofmt -l` 空
- [x] `go test ./...`（无 integration tag）全绿
- [x] 集成测试（§四）对本地全栈绿
- [x] README 含 `go get github.com/voidvec/fulla/clients/go` 用法

---

## 四、W4 — 集成测试（env 门控，本地全栈）

**环境**：PG13 native + Release 构建的服务器（:5555）+ 种子数据（`apps/server/seed/*.sql`，含 `backend-svc`/`test-secret`）。

**服务器启动流程**（复用 `run-endpoint-tests.ps1` 的成熟配方——`full_test.bat` 第 8 步会关服 + 结尾交互 pause，**不能**直接当启动器）：

```
1. scripts/backend/setup_database.bat        # 建库 + 迁移 + 全部种子（含 backend-svc）
2. kill 残留 fulla-server 进程
3. Start-Process fulla-server.exe（工作目录 = exe 所在目录，同 run_server.bat）
4. 轮询 GET /health/live 直到 200（上限 30s）
5. 跑两语言集成测试（FULLA_BASE_URL=http://127.0.0.1:5555）
6. finally: Stop-Process
```

**门控**：`FULLA_BASE_URL`（默认 unset → skip；设 `http://127.0.0.1:5555` 启用）。凭证 `FULLA_CLIENT_ID`/`FULLA_CLIENT_SECRET`（默认 backend-svc/test-secret）。

**用例矩阵（Python 与 Go 对称）**：

| # | 用例 | 断言 |
|---|------|------|
| I1 | client_credentials 取 token | 200，access_token 非空，token_type=Bearer，scope=tokens:read（显式请求） |
| I2 | introspect 自省 | 200，active=true，client_id=backend-svc（用生成客户端调用） |
| I3 | discovery 拉取 | openid-configuration 200，issuer == `http://localhost:5555`（DiscoveryController 在未配置 `metadata.issuer` 时回退此硬编码默认——断言与代码行为一致，勿与 base_url 混用） |
| I4 | 负例：M2M token 调 userinfo | 401（invalid_token——client_credentials token 无用户身份） |
| I5 | 负例：错 secret 取 token | 401 invalid_client → auth 层错误类型 |

**验收**：
- [x] 服务器运行中，Python `pytest tests/integration` 5 用例绿
- [x] 服务器运行中，Go `go test -tags integration ./...` 5 用例绿

---

## 五、W3 — regen 工具 + 漂移门 CI

### 5.1 `tools/clients/regen_clients.py`

- 常量 pin：`OPENAPI_PYTHON_CLIENT_VERSION = "0.29.0"`、`OAPI_CODEGEN_VERSION = "v2.8.0"`。
- `--check`：生成到临时目录 → 与 `clients/*/src|generated` 逐文件比对（忽略 `.ruff_cache`、`__pycache__` 等）→ 漂移则打印 diff 概要并 exit 1。
- `--python-only` / `--go-only`：单语言模式（CI 两个 job 分别用）。
- `--version-only`：只做版本联动检查（release.yml 的 `sdk-python` job 用）。
- 附加检查（总是跑）：`clients/python/pyproject.toml` 的 `version` == `cmake/Version.cmake` 三段拼接。
- 调用形态：Python 侧 `python -m openapi_python_client`（要求已安装于当前环境；CI 在 venv 里 `pip install openapi-python-client==0.29.0`）；Go 侧 `go run github.com/oapi-codegen/oapi-codegen/v2/cmd/oapi-codegen@v2.8.0`。
- 遵循仓库脚本惯例：`--selftest`（小 fixture）、退出码 0/1/2、ASCII 输出。

### 5.2 `.github/workflows/clients-sdk.yml`

- 触发：PR（paths: `clients/**`、`tools/clients/**`、`apps/server/openapi.yaml`、workflow 自身）+ master push（同 paths）+ workflow_dispatch。
- jobs：
  - `python`：setup-python 3.12 → venv 安装 `openapi-python-client==0.29.0` + `pip install -e 'clients/python[dev]'`（**引号**防 bash glob）→ `python tools/clients/regen_clients.py --check --python-only` → `pytest clients/python/tests`
  - `go`：setup-go（stable，缓存 go.sum）→（`working-directory: clients/go`）`go build ./... && go vet ./...` → `python tools/clients/regen_clients.py --check --go-only` → `go test ./...`
- 不进 static-checks（工具链依赖），不阻塞无关 PR（paths 过滤）。

### 5.3 验收标准

- [x] `regen_clients.py` 幂等：连跑两次 git status 干净
- [x] 故障注入：手改 `generated/` 一行 → `--check` exit 1
- [x] 故障注入：pyproject version 改错 → 版本联动检查 exit 1
- [x] workflow YAML 语法（actionlint v1.7.12 对 `clients-sdk.yml` + `release.yml` 零告警——后者在本分支从未被 CI 触发过，靠静态检查兜底）通过；本 PR 上两个 job 绿（PR #65 "Python client (drift + unit)" / "Go client (drift + build + unit)" 均 SUCCESS）

---

## 六、W5 — 发布流水线接线（M3）

`release.yml` 新增 job `sdk-python`（`needs: [version-check]`，仅 tag）：

```
1. checkout + setup-python
2. 版本一致性前置校验：python tools/clients/regen_clients.py --version-only
   && pyproject version == ${{ env.VERSION }}（防漏 bump 发布错版）
3. pip install build → python -m build clients/python（sdist+wheel）
4. pypa/gh-action-pypublish@release/v1
   — secret 绑 job 级 env: PYPI_API_TOKEN；publish step 上 if: env.PYPI_API_TOKEN != ''
   — 未配置时 step 显式 skip 并打印设置指引（不 fail；secrets context 不能进 job 级 if）
```

- Go proxy：`github-release` job 末尾增加一步——在发布 commit 上创建并推送嵌套 tag `clients/go/v${VERSION}`（子目录模块版本解析要求；该形态不匹配 release.yml 的 tag 触发模式，无递归触发）。README 说明 module path 无 `/vN` 后缀 ⇒ 项目 v2 前需迁 `.../clients/go/v2`。
- 发布文档（clients README 附录）：PyPI 项目注册 + secret 配置的一次性步骤。

**验收**：
- [ ] `workflow_dispatch` dry-run：sdk-python job 走到 publish 步骤并按 secret 缺失 skip（不 fail）；其余 job 不受影响（待合并后从 Actions UI dispatch 验证。2026-08-19 曾尝试在 PR 分支预触发：gh PAT 无 Actions 写权限被 403 拒绝，IAB 无 GitHub 会话亦不可行；静态侧已由 actionlint 全量通过兜底，publish 步骤的门控 `if: github.ref_type == 'tag' && env.PYPI_API_TOKEN != ''` 在 dispatch（branch ref）下必走 skip 分支）
- [x] 本地 `python -m build clients/python` 产物完整（sdist 含 generated/，wheel 含 py.typed 等）

---

## 七、W6 — 文档同步

| 文档 | 改动 |
|------|------|
| `README.md` / `README.zh-CN.md` | 新增「Client SDKs（Python/Go）」小节：安装（pip/go get）、指向 clients/*/README；措辞不超出已验证事实（PyPI 首次发布前写"随 vNext tag 发布"） |
| `docs/productization-evolution/next-phase-implementation-plan.md` | C1 状态更新（M1/M2/M4 交付、M3 wiring 完成、发布待 secret） |
| `docs/productization-evolution/progress-status.md` | 进度行 |
| `docs/productization-evolution/README.md` | client-sdk 设计条目状态（已在本次移动中更新链接） |
| 设计文档 §六 AC 表 | AC4/AC5/AC6 勾选（附证据指针）；AC7 标注"发布验收，待 tag" |

---

## 八、实施顺序与提交切分

```
1. W3 regen 脚本 + selftest                        → commit 1（工具先行，后续提交靠它产出）
2. W1 Python 客户端（生成物 + auth + 单测 + 示例）    → commit 2
3. W2 Go 客户端（生成物 + auth + 单测）              → commit 3
4. W4 集成测试（两语言）+ CI workflow（W3.2）        → commit 4
5. W5 release.yml 接线                              → commit 5
6. W6 文档同步 + 验收证据                            → commit 6
```

验证序列（实施完成后）：
1. 单测：pytest + go test
2. 集成：按 §四启动流程起全栈 → 两语言集成测试
3. 全量测试：`full_test.bat` 8 步 + 前端（admin 16 e2e + user 8 e2e + 单测）
4. CI 静态检查本地核实：arch-guard、migration-check、api-diff、naming、parity、openapi-spec-validator、check_spec_governance（selftest+run）、clients-sdk.yml 两个 job 的本地等价命令
5. 补充本地核实（评审 B5）：`scripts/security-check.sh`（CI 每 PR 跑）+ `scripts/check-doc-links.sh`（无 workflow 接线，手动跑；扫描全仓 .md，新 README 链接靠它验证）
6. 发 PR → 自评审（code-reviewer）→ 修复 → 等待 CI

---

## 九、风险与回退

| 风险 | 缓解 |
|------|------|
| 生成器输出不稳定（同版本两次生成 diff） | 实测验证幂等；regen 脚本 pin 版本；`--check` CI 门 |
| httpx <0.29 上限与未来冲突 | 依赖范围镜像生成器声明；升级走独立 PR（承设计 §九） |
| Python 3.14 本地 vs CI 3.12 差异 | requires-python >=3.11；CI 用 3.12，本地 3.14 双覆盖 |
| release.yml 改动破坏既有发布 | publish 步 env-guard skip；dry-run 验证；嵌套 tag 步骤独立可回退；不改既有 job 语义 |
| oasdiff 门误触发 | 本任务不改 openapi.yaml，PR 上该门无 diff |
| PyPI 项目被抢注 | `fulla-oauth2` 已核实可用（404）；PR 合并后尽快注册 |
| Go proxy 拉取（国内网络） | README 记录 GOPROXY 镜像提示；CI 不受影响 |
| 嵌套 tag `clients/go/vX.Y.Z` 与未来 tag 规范冲突 | release skill/发布文档同步说明；tag 形态进 PR 描述供评审确认 |

---

## 附录 A：评审记录（2026-08-18，code-architect 深度评审）

阻塞项 5 条，全部采纳修订：
1. Go 子目录模块需嵌套 tag（根 tag 不解析）→ §六已改为 github-release 内创建 `clients/go/v${VERSION}`
2. I3 issuer 断言错误（未配置 `metadata.issuer` 时服务端回退硬编码 `http://localhost:5555`）→ §四已改
3. W4 无服务器启动流程（full_test.bat 会关服 + 交互 pause）→ §四已写明 run-endpoint-tests.ps1 配方
4. `secrets` context 不能进 job 级 `if:` → 设计 §11.6 + 计划 §六已改为 job env + step 级门控
5. 发布时缺 pyproject 版本校验 → §六已加 `--version-only` 前置校验

非阻塞项 6 条，采纳：authcode Basic 显式化 + G8 用例（B2）、.gitignore 补 `.pytest_cache/.ruff_cache/*.egg-info`（B3）、pip extras 引号 + Go working-directory + `--python-only/--go-only` 定义（B4）、本地核实清单补 security-check/check-doc-links（B5）、401 重试带 body 用例（B6）。
B1（openapi.yaml `client_id/client_secret form fields` 描述与 F-017 表述张力）**不在本 PR 修**（不触碰 YAML 的范围约束）；作为后续 spec 文案澄清 issue 记录进 PR 描述。
