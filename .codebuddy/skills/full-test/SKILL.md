---
name: full-test
description: 全量测试 — 后端 8 步（full_test.bat/full-test.sh）+ 前端（admin e2e 16 + user e2e 8 + 单元测试 5 含 3 属性）
---

# 全量测试

先跑全量后端测试，再跑全量前端测试。两个都通过才算通过。

## 步骤

1. **执行 `/full-backend-test`** — 后端 8 步流水线（DB重置→ORM→构建→ctest→服务器→59+52 端点测试→关闭）
2. **执行 `/full-frontend-test`** — 前端（Admin: build[tsc&&vite build]+16 e2e；User: build[vite build]+8 e2e+test:unit 5 文件[含 3 属性测试]）

## 通过标准

后端 8/8 PASS + 前端 0 FAIL = 全量测试通过。
