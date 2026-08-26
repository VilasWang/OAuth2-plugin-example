---
title: 档案 · 专业仓库改造审计（2026-08-25）
date: 2026-08-25
status: 已执行（chore/professional-repo-cleanup 落地）
sidebar_label: 档案 · 仓库改造审计
---

# 专业仓库改造审计 — 入库范围清理清单

> 审计日期：2026-08-25 ｜ 方法：`git ls-files` 全量盘点 + 体积/敏感内容/生成物交叉筛查
> 背景：仓库转入专业化运营（配合 authforge→fulla 更名，见 [rename-impact-fulla.md](rename-impact-fulla.md) §7 Phase 1）

## 一、必须从远程去除的文件（untrack + gitignore，磁盘保留）

| # | 目录/文件 | 规模 | 性质 | 处置 |
|---|---|---|---|---|
| 1 | `.qoder/` | 257 文件 / ~150KB（含 1.1MB repowiki 元数据） | Qoder AI 工具工作区：repowiki 为**生成的仓库 wiki**（201 页，改名时还会产生 800+ 处替换噪音） | `git rm -r --cached` + ignore |
| 2 | `.codebuddy/` | 45 文件 | `.claude/rules/`+skills 的**镜像副本**（内容一致，纯冗余、有漂移风险） | 同上 |
| 3 | `.zcode/` | 21 文件 | ZCode 工具专属：skills 镜像 + 2 个旧 plans（`plans/` 已在 .gitignore，这 2 个是规则生效前的漏网） | 同上 |
| 4 | `.kiro/` | 23 文件 | Kiro 工具专属 specs —— 但其中 **6 个 spec 的 design/requirements/tasks 是有价值的工程设计文档**，先迁移再去除 | **先迁 `docs/history/design/kiro-specs/`**（`.config.kiro` 工具状态文件不迁），再 untrack + ignore |
| 5 | `.claude/MEMORY.md` | 1 文件 | 2025-04 断代的**过期个人自动化记忆**（自称"OAuth2 插件示例项目"，与现状严重失配） | 直接 `git rm`（删除） |

**合计**：约 347 个文件出库。所有工具目录磁盘副本保留（本地工具流不受影响），仅退出版本库。

**去除理由归纳**：专业仓库入库标准 = 对所有贡献者有复现/协作价值的产物。AI 工具的个人工作区、可再生成物、多副本镜像均不符合；`.claude/`（规则权威源）作为唯一例外保留，见下。

## 二、保留清单（审查过但有明确专业理由，防御性记录）

| 项 | 规模 | 保留理由 |
|---|---|---|
| `.claude/`（除 MEMORY.md） | 37 文件（rules 4 / skills 19 / agents 7 / commands 6 / settings） | AGENTS.md 声明的**规则权威源**，等价于贡献者工作流文档；settings.json 权限/钩子配置合理 |
| `.vscode/` | 5 文件 | 共享 IDE 配置（launch/tasks/c_cpp + `settings.local.json.example` 模板）；`*.local.json` 已 ignore |
| `benchmarks/results/` | 885 文件 / 仅 756KB | README 明示"从提交的 JSON 再生对比报告"的**可复现性设计**，非垃圾数据 |
| `docs/backend/api/swagger-ui/` vendored bundle | ~3.9MB | 离线可用的 API 文档（标准 vendoring 做法） |
| `tools/api-diff/api-baseline.txt` | 300KB | CI API 门禁基线（改名时按计划重生成） |
| `clients/go/generated/`、`frontends/*/package-lock.json`、`conan.lock` | — | 生成 SDK 是交付物本体；锁文件是专业仓库标配 |

## 三、需要调整（不出库，但要改）

1. **`.gitignore` 增补**：`.qoder/`、`.codebuddy/`、`.zcode/`、`.kiro/`、`.workbuddy/`（`.workbuddy/` 当前未忽略，git status 长期裸奔）；
2. **`AGENTS.md`「各 AI 工具目录的角色」表**：从"多工具镜像同步"改为"`.claude/` 唯一权威 + 其余工具目录本地化"；
3. **机器绝对路径泛化（共 3 处，已执行）**：`.claude/skills/full-backend-test/SKILL.md:20` 的 `cd /d/work/...` → `cd "$(git rev-parse --show-toplevel)"`；`docs/history/design/http-integration-test-coverage-plan.md:10` 的本地 Drogon 检出路径 → 通用描述；`docs/history/design/superpowers/specs/2026-04-14-multiplatform-ci-design.md:519` 的 `file:///D:/...` 本地链接 → 纯文本引用（初扫误报的 `docs/ops/deployment-windows-docker-desktop.md` 复核后无个人路径，未改）；
4. （随改名 PR 处理，不在本清单执行）README owner 统一、GitHub topics `rabc` 拼写等见 rename-impact §2bis C。

## 四、审计确认无问题项

- **零密钥/证书/凭据入库**：无 `*.pem/*.key/*.crt` 跟踪；`PRIVATE KEY` 零命中；`frontends/user/.env`（含 GitHub client id 等）已被 .gitignore 正确拦截；
- **零构建产物/IDE 垃圾**：dist/build/node_modules/coverage 均未跟踪；
- **`.claude/settings.local.json` 未入库**（仅 `.vscode` 有 `.example` 模板，处理正确）；
- 现有 `.gitignore` 本身已相当完善（本审计仅增补上述 5 条）。

## 五、执行记录（Phase 1 实际命令）

```bash
git switch -c chore/professional-repo-cleanup          # 基于 origin/master (e973a4f8)
git mv .kiro/specs/<6 个 spec 目录> docs/history/design/kiro-specs/   # 迁移设计文档
git rm docs/history/design/kiro-specs/*/.config.kiro   # 工具状态文件不迁
git rm -r --cached .qoder .codebuddy .zcode            # 出库（磁盘保留）
git rm .claude/MEMORY.md                               # 过期记忆删除
# + .gitignore 增补、AGENTS.md 角色表更新、绝对路径泛化
```
