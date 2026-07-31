---
name: error-handling-compliance-audit
overview: 调研 authforge 代码库中所有不满足三条错误处理规则的地方：DB操作缺少DrogonDbException捕获、异步回调缺少failure path、异步回调缺少try-catch，输出按规则分类的违规报告。
todos:
  - id: v1
    content: "Rule2违规 — AuthService.cc:88: [resetUser](const DrogonDbException &) {} 空的错误处理器，吞掉了重置失败登录计数失败的异常。位于 authenticateUser() 方法中，成功登录后重置 failedLoginCount——操作为非关键 fire-and-forget，但严格违反规则2"
    status: completed
  - id: v2
    content: "Rule2违规 — AuthService.cc:168: [failedUser](const DrogonDbException &) {} 空的错误处理器，吞掉了增加失败登录计数失败的异常。位于 authenticateUser() 方法中，密码验证失败后递增计数——操作为非关键 fire-and-forget，但严格违反规则2"
    status: completed
  - id: v3
    content: "Rule2违规 — WebAuthnController.cc:563: [](const DrogonDbException &) {} 空的错误处理器，吞掉了更新 sign_count 和 last_used_at 失败的异常。位于 authenticateFinish() 中，验证成功后更新签名计数——操作为非关键 fire-and-forget，但严格违反规则2"
    status: completed
  - id: v4
    content: "Rule2违规 — OAuth2StandardController.cc:1647: [](const DrogonDbException &) {} 空的错误处理器，吞掉了删除已使用的 device_code 失败的异常。位于 token() 端点的 device_code grant 流程中——操作为非关键清理 fire-and-forget，但严格违反规则2"
    status: completed
  - id: v5
    content: Rule3风险点排查与全局审计 — 确认 Storage 层和 Services 层的异步回调在多 Mapper 嵌套场景下的 try-catch 覆盖情况，以及非 DB 异常（std::invalid_argument、Json 解析异常等）的处理
    status: completed
    dependencies:
      - v1
      - v2
      - v3
      - v4
---

## 调查结果：Error Handling 三条规则合规性审计

对 authforge 整个代码库（排除 test/ 目录和 models/ ORM 生成文件）逐文件审查了三条错误处理规则的遵守情况。

### 规则1: Always catch `const DrogonDbException &e` for DB operations

**结论：合规** ✅

所有生产代码中的 Mapper::findBy、Mapper::findOne、Mapper::insert、Mapper::update、Mapper::deleteBy、execSqlAsync 调用均提供了 DrogonDbException 错误处理回调。未发现任何 Mapper 调用缺少 error callback 的情况。

涵盖的目录：

- OAuth2Plugin/src/storage/（6个 Repository 文件）
- libs/storage-postgres/src/（4个 Repository 文件）
- libs/drogon/src/admin/（5个 Admin Service 文件）
- libs/drogon/src/services/（5个 Service 文件）
- libs/drogon/src/controllers/（8个 Controller 文件）
- OAuth2Server/src/organization/（OrganizationService）

### 规则2: All async callbacks MUST handle failure path: `(*sharedCb)(errorResult)`

**结论：发现 4 处违规** 🔴（均为 fire-and-forget 模式）

### 规则3: Always need try catch for all async callbacks

**结论：基本合规** ✅（仅 Storage 层有一个已知风险点）