# 项目技能现代化升级设计文档

**日期**: 2026-05-18  
**项目**: authforge  
**目标**: 更新项目级技能以匹配重构后的项目结构

---

## 1. 背景分析

### 1.1 项目重构概述

项目经历了重大架构重构，从单一目录结构重组为模块化架构：

**重构前结构**:
```
OAuth2Backend/          # 单体应用目录
├── controllers/
├── models/
├── sql/
├── build/
└── scripts/
```

**重构后结构**:
```
OAuth2Plugin/           # 核心插件库
├── include/
├── src/
└── models_backup/

OAuth2Server/           # 示例应用
├── controllers/
├── sql/
├── model.json
├── config.json
└── openapi.yaml

scripts/backend/        # 统一脚本目录
manage.ps1              # 统一管理接口
```

### 1.2 技能过时问题识别

**关键问题**:
1. **路径引用错误**: 所有技能仍引用 `OAuth2Backend/`，实际应为 `OAuth2Server/`
2. **脚本路径不正确**: 技能中直接引用脚本，缺少 `scripts/backend/` 前缀
3. **构建路径过时**: 构建输出在 `build/OAuth2Server/` 而非 `build/`
4. **管理接口缺失**: 未利用新增的 `manage.ps1` 统一管理接口
5. **Docker集成不完整**: 未充分利用新的 Docker 专项脚本

**影响的技能**:
- `build-and-test` - 构建和测试技能
- `db-reset` - 数据库重置技能
- `orm-gen` - ORM 生成技能
- `openapi-update` - OpenAPI 更新技能
- `e2e-test` - 端到端测试技能
- `docker-integration-test` - Docker 集成测试技能

---

## 2. 现代化升级设计

### 2.1 核心路径映射

**路径转换表**:
```yaml
# 目录结构映射
OAuth2Backend/              → OAuth2Server/
OAuth2Backend/build/        → build/OAuth2Server/
OAuth2Backend/sql/          → OAuth2Server/sql/
OAuth2Backend/models/       → OAuth2Server/models/
OAuth2Backend/controllers/  → OAuth2Server/controllers/

# 脚本路径映射
build.bat                   → scripts/backend/build.bat
build.sh                    → scripts/backend/build.sh
generate_models.bat         → scripts/backend/generate_models.bat
docker_postgres_start.bat   → scripts/backend/docker_postgres_start.bat
full_test_docker.bat        → scripts/backend/full_test_docker.bat

# 统一管理接口
./manage.ps1 build-backend  → 替代直接调用 scripts/backend/build.bat
./manage.ps1 test-backend   → 替代直接调用 ctest
./manage.ps1 docker-up      → 替代直接调用 docker-compose up -d
```

### 2.2 技能分层架构

**🔧 基础技能层** (可独立使用)
- `build-and-test` - 构建和测试 OAuth2Server
- `db-reset` - 数据库重置和初始化
- `orm-gen` - ORM 模型重新生成
- `openapi-update` - OpenAPI 规范更新

**🚀 高级技能层** (组合基础技能)
- `e2e-test` - 完整端到端测试流程
- `docker-integration-test` - Docker 环境集成测试

**⚡ 统一接口层**
- 所有技能优先使用 `manage.ps1` 接口
- 提供向后兼容的直接脚本调用方式
- 智能环境检测和降级处理

### 2.3 现代化集成特性

#### 2.3.1 manage.ps1 集成

**优先使用统一接口**:
```powershell
# 构建相关
.\manage.ps1 build-backend           # 默认 Release 构建
.\manage.ps1 build-backend -debug    # Debug 构建

# 测试相关  
.\manage.ps1 test-backend            # 运行后端测试

# Docker 相关
.\manage.ps1 docker-up               # 启动 Docker Compose
.\manage.ps1 docker-down             # 停止 Docker Compose

# 前端相关
.\manage.ps1 dev-frontend            # 前端开发模式
.\manage.ps1 build-frontend          # 前端生产构建
```

#### 2.3.2 Docker 脚本优化

**利用新的 Docker 专项脚本**:
```bash
# 智能 PostgreSQL 管理
scripts/backend/docker_postgres_start.bat   # 智能启动并等待就绪
scripts/backend/docker_postgres_stop.bat    # 优雅停止

# 完整 Docker 测试流程
scripts/backend/full_test_docker.bat        # 一键完整测试
# 包含: PostgreSQL启动 → 数据库初始化 → ORM生成 → 构建 → 测试 → 服务启动 → API测试 → 清理
```

#### 2.3.3 跨平台支持

**平台自适应**:
```yaml
Windows 环境:
  主接口: manage.ps1
  备用接口: scripts/backend/*.bat

Linux/macOS 环境:
  主接口: scripts/backend/*.sh
  兼容性: 自动检测系统包管理器 (apt/brew)
```

---

## 3. 详细技能升级规范

### 3.1 build-and-test 技能

**现代化特性**:
- 使用 `.\manage.ps1 build-backend [-debug]` 作为主接口
- 支持新构建路径 `build/OAuth2Server/`
- 智能检测编译器环境 (MSVC/GCC/Clang)
- 集成 CI/CD 环境变量支持

**工作流程**:
```bash
# 1. 环境检查
检查 Drogon 框架、编译器、依赖库

# 2. 优先使用统一接口
.\manage.ps1 build-backend -release

# 3. 备用直接调用 (向后兼容)
scripts/backend/build.bat -release      # Windows
scripts/backend/build.sh Release        # Linux/macOS

# 4. 运行测试
.\manage.ps1 test-backend
或: cd build/OAuth2Server && ctest --output-on-failure
```

**路径修复清单**:
- [ ] `OAuth2Backend/` → `OAuth2Server/`
- [ ] `build/` → `build/OAuth2Server/`
- [ ] 集成 `manage.ps1` 接口
- [ ] 更新构建输出路径引用

### 3.2 db-reset 技能

**现代化特性**:
- 修复 SQL 路径: `OAuth2Server/sql/*.sql`
- 集成 Docker 模式支持
- 智能环境检测 (本地PostgreSQL/Docker/CI)
- 增强安全确认机制

**工作流程**:
```bash
# 1. 环境检测
检测运行环境:
  - Docker 模式: docker-compose.yml 运行中
  - 本地模式: PostgreSQL 在 localhost:5432
  - CI 模式: 环境变量检测

# 2. 停止服务 (防止连接冲突)
.\manage.ps1 docker-down  # Docker 模式
taskkill /F /IM OAuth2Server.exe  # Windows 本地模式

# 3. 数据库重置
Docker 模式:
  scripts/backend/docker_postgres_start.bat  # 自动重建数据库

本地模式:
  psql -c "DROP DATABASE IF EXISTS oauth2_db;"
  psql -c "CREATE DATABASE oauth2_db;"
  psql -d oauth2_db -f OAuth2Server/sql/001_oauth2_core.sql
  psql -d oauth2_db -f OAuth2Server/sql/002_users_table.sql
  psql -d oauth2_db -f OAuth2Server/sql/003_rbac_schema.sql
  psql -d oauth2_db -f OAuth2Server/sql/004_oauth2_scopes.sql
```

**路径修复清单**:
- [ ] `OAuth2Backend/sql/` → `OAuth2Server/sql/`
- [ ] 集成 `docker_postgres_start.bat`
- [ ] 添加环境自动检测
- [ ] 更新服务停止命令

### 3.3 orm-gen 技能

**现代化特性**:
- 修复模型配置路径: `OAuth2Server/model.json`
- 使用 `scripts/backend/generate_models.bat`
- 支持新插件结构 `OAuth2Plugin/`
- 智能验证生成的一致性

**工作流程**:
```bash
# 1. 前置检查
检查 PostgreSQL 连接
检查数据库 schema 是否最新
检查 drogon_ctl 工具可用性

# 2. 配置路径修复
旧: OAuth2Backend/models/model.json
新: OAuth2Server/model.json

# 3. 生成 ORM
.\manage.ps1 build-backend  # 确保项目最新
scripts/backend/generate_models.bat -y  # 自动确认

# 4. 验证生成结果
检查新生成的 ORM 类
验证编译通过
```

**路径修复清单**:
- [ ] `OAuth2Backend/models/model.json` → `OAuth2Server/model.json`
- [ ] 使用 `generate_models.bat` 脚本
- [ ] 更新 ORM 类输出路径
- [ ] 添加生成验证步骤

### 3.4 e2e-test 技能

**现代化特性**:
- 修复所有路径引用
- 集成 `full_test_docker.bat` 完整流程
- 支持多环境测试 (本地/Docker/CI)
- 增强错误报告和日志输出

**工作流程**:
```bash
# 1. 环境选择
自动选择最佳测试环境:
  - Docker 环境: 优先使用 full_test_docker.bat
  - 本地环境: 使用传统测试流程
  - CI 环境: 使用 CI 优化的流程

# 2. Docker 模式 (推荐)
scripts/backend/full_test_docker.bat
# 自动完成: PostgreSQL启动 → 数据库初始化 → ORM生成 → 构建 → 测试 → API测试 → 清理

# 3. 本地模式
.\manage.ps1 build-backend
.\manage.ps1 test-backend
.\manage.ps1 docker-up  # 如果需要完整环境

# 4. 端到端测试流程
登录 → 授权码获取 → Token交换 → API访问 → 权限验证 → 清理
```

**路径修复清单**:
- [ ] 修复所有 `OAuth2Backend/` 引用
- [ ] 集成 `full_test_docker.bat`
- [ ] 更新测试数据路径
- [ ] 修复服务启动/停止路径

### 3.5 docker-integration-test 技能

**现代化特性**:
- 充分利用 `docker-compose.yml` 多服务架构
- 集成新的 Docker 专项脚本
- 支持完整的微服务集成测试
- 生成详细的 HTML 测试报告

**Docker 服务架构**:
```yaml
docker-compose.yml 服务:
  - oauth2-frontend:     Vue 前端 (8080)
  - oauth2-backend:      Drogon 后端 (5555)  
  - oauth2-postgres:     PostgreSQL (5433)
  - oauth2-redis:        Redis (6380)
  - prometheus:          监控 (9090)
```

**工作流程**:
```bash
# 1. 环境准备
.\manage.ps1 docker-up

# 2. 健康检查
检查所有服务状态: docker-compose ps
验证服务就绪: pg_isready, redis-cli ping

# 3. 完整集成测试
scripts/backend/full_test_docker.bat

# 4. 端到端测试
前端 → 后端 → 数据库 → Redis 完整流程测试

# 5. 性能和监控测试
Prometheus 指标收集
性能基准测试
负载测试

# 6. 报告生成
python .claude/skills/docker-integration-test/scripts/generate_report.py

# 7. 清理
.\manage.ps1 docker-down
```

**路径修复清单**:
- [ ] 更新 `docker-compose.yml` 服务引用
- [ ] 集成 `full_test_docker.bat`
- [ ] 修复容器名称和端口
- [ ] 更新测试报告路径

### 3.6 openapi-update 技能

**现代化特性**:
- 修复控制器路径: `OAuth2Server/controllers/`
- 更新 OpenAPI 规范路径: `OAuth2Server/openapi.yaml`
- 支持新的模块化架构
- 集成自动验证流程

**工作流程**:
```bash
# 1. 分析控制器
读取路径更新:
  - OAuth2Server/controllers/OAuth2Controller.cc
  - OAuth2Server/controllers/WeChatController.cc
  - OAuth2Server/controllers/AdminController.cc

# 2. 比较规范
旧: OAuth2Backend/openapi.yaml
新: OAuth2Server/openapi.yaml

# 3. 更新规范
添加新端点
更新参数定义
修正响应模型

# 4. 验证
scripts/backend/validate-openapi.sh  # Linux/macOS
或手动 YAML 语法检查
```

**路径修复清单**:
- [ ] `OAuth2Backend/controllers/` → `OAuth2Server/controllers/`
- [ ] `OAuth2Backend/openapi.yaml` → `OAuth2Server/openapi.yaml`
- [ ] 更新端点路径引用
- [ ] 集成验证脚本

---

## 4. 实施计划

### 4.1 阶段 1: 路径修复 (立即生效)

**目标**: 修复所有过时的路径引用

**具体任务**:
1. 批量替换所有技能文件中的路径:
   - `OAuth2Backend/` → `OAuth2Server/`
   - `OAuth2Backend/build/` → `build/OAuth2Server/`
   - `OAuth2Backend/sql/` → `OAuth2Server/sql/`

2. 更新脚本路径引用:
   - 添加 `scripts/backend/` 前缀
   - 修复相对路径计算

3. 修复构建输出路径:
   - `build/Release/` → `build/OAuth2Server/Release/`
   - `build/Debug/` → `build/OAuth2Server/Debug/`

**预期成果**: 所有技能能够正确引用新的项目结构

### 4.2 阶段 2: manage.ps1 集成 (提升体验)

**目标**: 优先使用统一管理接口

**具体任务**:
1. 更新所有技能优先使用 `manage.ps1`:
   ```powershell
   # 构建优先使用
   .\manage.ps1 build-backend [-debug]
   
   # 测试优先使用  
   .\manage.ps1 test-backend
   ```

2. 添加向后兼容的备用调用:
   ```bash
   # 当 manage.ps1 不可用时降级
   scripts/backend/build.bat -release
   scripts/backend/build.sh Release
   ```

3. 优化错误处理和日志输出:
   - 统一错误格式
   - 改进日志可读性
   - 添加调试信息

**预期成果**: 统一的开发体验，更好的错误处理

### 4.3 阶段 3: Docker 优化 (充分利用新脚本)

**目标**: 集成新的 Docker 专项脚本

**具体任务**:
1. 集成新的 Docker 脚本:
   ```bash
   # PostgreSQL 管理
   scripts/backend/docker_postgres_start.bat
   scripts/backend/docker_postgres_stop.bat
   
   # 完整测试流程
   scripts/backend/full_test_docker.bat
   ```

2. 支持完整的多服务测试:
   - 前端 + 后端 + 数据库 + Redis + 监控
   - 服务间通信测试
   - 完整的 OAuth2 流程测试

3. 优化 Docker Compose 工作流:
   - 使用 `.\manage.ps1 docker-up/down`
   - 支持服务健康检查
   - 智能等待服务就绪

**预期成果**: 充分利用 Docker 的集成测试能力

### 4.4 阶段 4: 测试验证 (确保稳定性)

**目标**: 在不同平台验证所有技能

**具体任务**:
1. **跨平台测试**:
   - Windows 11 (MSVC 2022)
   - Ubuntu 22.04 (GCC 11)
   - macOS 14 (Clang ARM64)

2. **功能验证**:
   - 每个技能的独立测试
   - 技能组合测试
   - 边界条件测试

3. **向后兼容性测试**:
   - 验证降级机制工作正常
   - 确保旧命令仍然可用
   - 测试错误处理路径

**预期成果**: 所有技能在支持平台上稳定工作

---

## 5. 风险评估与缓解

### 5.1 主要风险

**风险 1: 路径替换导致功能失效**
- **影响**: 高 - 可能导致技能无法找到必要文件
- **概率**: 中 - 路径变化较大
- **缓解**: 
  - 仔细检查每个路径引用
  - 添加文件存在性检查
  - 提供详细的错误消息

**风险 2: manage.ps1 接口变更**
- **影响**: 中 - 统一接口可能发生变化
- **概率**: 低 - 接口相对稳定
- **缓解**:
  - 保持向后兼容的备用调用
  - 添加接口版本检查
  - 提供降级机制

**风险 3: 跨平台兼容性问题**
- **影响**: 中 - 某些平台可能出现特定问题
- **概率**: 中 - 跨平台差异客观存在
- **缓解**:
  - 在所有支持平台上测试
  - 使用平台检测机制
  - 提供平台特定的变通方案

### 5.2 回滚计划

**如果出现严重问题**:
1. 保留原始技能文件的备份
2. 使用 Git 快速回滚到之前版本
3. 提供手动修复指南
4. 逐步重新应用更新

---

## 6. 成功标准

### 6.1 功能完整性
- ✅ 所有技能能够正确执行其核心功能
- ✅ 路径引用 100% 正确
- ✅ no broken references 或过时内容

### 6.2 用户体验
- ✅ 统一的管理接口 (`manage.ps1`)
- ✅ 清晰的错误消息和日志
- ✅ 向后兼容性保持

### 6.3 平台支持
- ✅ Windows 11 完全支持
- ✅ Ubuntu 22.04 完全支持  
- ✅ macOS 14 完全支持

### 6.4 性能和稳定性
- ✅ 所有测试通过
- ✅ 无回归问题
- ✅ 错误处理健壮

---

## 7. 后续优化方向

### 7.1 短期优化 (1-2 个月)
- 添加更多智能检测机制
- 优化错误恢复流程
- 增强日志和调试功能

### 7.2 中期优化 (3-6 个月)
- 引入新的"完整工作流"技能
- 支持更多的开发场景
- 集成更多的开发工具

### 7.3 长期优化 (6-12 个月)
- 完全重构技能架构以支持模块化
- 引入 AI 辅助的技能推荐
- 支持插件式技能扩展

---

## 8. 总结

这个现代化升级设计将确保项目技能与重构后的项目结构完全同步，充分利用新的架构优势，提供统一的开发体验，并保持跨平台兼容性。

**核心价值**:
- 🎯 **准确性**: 消除所有过时内容
- 🚀 **现代化**: 充分利用项目重构优势
- 🔧 **可维护性**: 统一接口，易于维护
- 🌐 **兼容性**: 跨平台支持，向后兼容

**预期时间线**:
- 阶段 1: 1-2 天 (路径修复)
- 阶段 2: 2-3 天 (接口集成)
- 阶段 3: 3-4 天 (Docker 优化)
- 阶段 4: 2-3 天 (测试验证)

**总计**: 约 2 周完成全部升级工作