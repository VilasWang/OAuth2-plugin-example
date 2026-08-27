# 任务 E：GitHub issue 留痕（草稿，暂不发布）

> 以下评论草稿待 PR #115 定稿后再通过 `gh issue comment` 发布。

---

## #88 评论草稿

```
PR #115 对本 issue 的部分处理（已完成）：

1. **数组 aud 候选项**：end_session 端点的 `id_token_hint` 验签现在支持 `aud` 为字符串或数组（RFC 7519 §4.1.3），服务端逐一尝试候选项校验 `post_logout_redirect_uri`。
2. **纯文本 400 → Error Envelope + 新错误码**：未注册的 `post_logout_redirect_uri` 从纯文本 400 迁移到 Error Envelope，错误码 `VALIDATION_REDIRECT_URI_NOT_REGISTERED`（3013）；缺 `id_token_hint` 返回 `AUTH_INVALID_ID_TOKEN_HINT`（4006）。api-reference.md 同步更新。

剩余项：
- **item 3**（前端 logout 携带 `id_token_hint`）：在决策批次处理，issue 保持 open。
```

---

## #90 评论草稿

```
PR #115 对本 issue 的部分处理：

**案例 1**（固定 sleep → 事件驱动等待）：已在 PR #115 完成。

**案例 2** 补充线索：本地调试确认 `0xc0000409` 在 MSVC 下也是"未捕获 C++ 异常 → `std::terminate` → `__fastfail`"的表现。本次具体场景是测试内 `execSqlSync` 列名错误抛 `DrogonDbException`，未被测试代码捕获，导致 `std::terminate` → `STATUS_STACK_BUFFER_OVERRUN (0xc0000409)`。排查 Contract 级联失败时优先找测试内未捕获异常。

issue 保持 open。
```

---

## #105 评论草稿

```
PR #115 对本 issue 的部分处理（已完成）：

- CI 覆盖率产出链路：`linux-coverage` preset + `coverage.yml` workflow + gcov JSON artifact + step summary（`measure_coverage.py` 聚合 per-library 行覆盖率）。
- 本次修复：Build 步骤因 `drogon_ctl: not found` 失败（Conan 包缓存不在 PATH），已在 coverage.yml 的 Configure 步骤注入 `drogon_ctl` 路径到 `$GITHUB_PATH`。

剩余项：
- Codecov 集成 / 棘轮（ratchet）仍为后续工作，issue 保持 open。
```

---

## 状态

- [x] 三条评论草稿已写好
- [ ] 待 PR #115 定稿后发布（`gh issue comment`）
