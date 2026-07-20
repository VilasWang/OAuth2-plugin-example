---
name: docker-integration-test
description: 在Docker Compose环境中执行OAuth2系统的完整集成测试和健康检查。当用户需要在代码变更后验证功能、运行端到端测试、检查Docker环境健康状态、或需要验证整个OAuth2系统（前端+后端+数据库+缓存）的集成时使用此技能。确保在Docker环境中测试所有组件的集成，包括健康检查、OAuth2流程、API端点、数据库连接、Redis缓存和前后端通信。
---

# Docker集成测试和健康检查

在Docker Compose环境中执行OAuth2系统的完整集成测试和健康检查，生成详细的HTML测试报告。

## 使用时机

- 代码变更后验证功能正常性
- 提交前运行完整测试套件
- 验证Docker环境部署状态
- 检查OAuth2系统集成问题
- 生成测试报告用于代码审查

## 测试范围

### 1. 环境健康检查
- Docker服务状态验证
- 网络连接检查
- 端口可用性确认
- 依赖服务就绪状态

### 2. OAuth2核心流程测试
- 授权码流程完整性
- Token生成和验证
- 客户端认证
- 用户登录流程
- 权限验证

### 3. API端点测试
- `/oauth2/authorize` - 授权端点
- `/oauth2/token` - 令牌端点
- `/oauth2/verify` - 验证端点
- 健康检查端点
- 管理API端点

### 4. 数据库集成测试
- PostgreSQL连接验证
- 数据模型完整性
- 事务处理正确性
- 查询性能检查
- 数据一致性验证

### 5. Redis集成测试
- 连接和认证测试
- 缓存读写功能
- 过期机制验证
- 性能基准测试

### 6. 前后端集成测试
- Vue前端与后端API通信
- OAuth2流程完整性
- 用户体验验证
- 错误处理正确性

## 测试流程

### 步骤 1: 环境准备（推荐使用脚本）

```bash
# 使用 Docker 专项脚本（推荐）
scripts/backend/full_test_docker.bat

# 此脚本会自动完成所有步骤：
# ✅ PostgreSQL 容器启动和就绪检测
# ✅ 数据库初始化和 schema 验证
# ✅ ORM 模型重新生成
# ✅ 项目构建（Release 配置）
# ✅ 单元测试和集成测试
# ✅ 服务启动和健康检查
# ✅ OAuth2 端点测试
# ✅ 清理和停止

# 如需手动控制，请继续下面的步骤
```

### 手动流程（可选）
```bash
# 停止现有容器
docker-compose down -v

# 使用统一管理接口启动
.\manage.ps1 docker-up

# 等待服务就绪（健康检查）
timeout /t 10 /nobreak
```

### 步骤 2: 健康检查
```bash
# 检查容器状态
docker-compose ps

# 检查服务日志
docker-compose logs oauth2-backend
docker-compose logs oauth2-frontend

# 验证端口可用性
curl -f http://localhost:5555/health || exit 1
curl -f http://localhost:8080 || exit 1
```

### 步骤3: 数据库初始化验证
```bash
# 连接PostgreSQL验证schema
docker exec oauth2-postgres psql -U oauth2_user -d oauth2_db -c "\dt"

# 验证初始化脚本执行
docker exec oauth2-postgres psql -U oauth2_user -d oauth2_db -c "SELECT COUNT(*) FROM oauth2_clients;"
```

### 步骤4: 后端集成测试
```bash
# 在容器中运行C++测试
docker exec oauth2-backend /bin/bash -c "cd build/OAuth2Server && ctest --output-on-failure -V"
```

### 步骤5: 前端集成测试
```bash
# 在容器中运行Vue测试
docker exec oauth2-frontend npm run test
```

### 步骤6: 端到端测试
```bash
# 测试OAuth2授权流程
# 1. 获取授权码
curl -X GET "http://localhost:5555/oauth2/authorize?response_type=code&client_id=vue-client&redirect_uri=http://localhost:8080/callback"

# 2. 交换token
curl -X POST "http://localhost:5555/oauth2/token" \
  -d "grant_type=authorization_code&code=xxx&client_id=vue-client"

# 3. 验证token
curl -X GET "http://localhost:5555/oauth2/verify" \
  -H "Authorization: Bearer xxx"
```

### 步骤7: 性能和负载测试
```bash
# 运行性能测试
docker exec oauth2-backend /bin/bash -c "cd build/OAuth2Server && ./AdvancedStorageTest"

# Redis性能测试
docker exec oauth2-redis redis-benchmark -h localhost -a redis_secret_pass
```

## 测试报告生成

使用bundle中的脚本生成HTML报告：

```bash
python .claude/skills/docker-integration-test/scripts/generate_report.py \
  --test-results ./test-results \
  --output ./test-results/docker-integration-test-report.html
```

报告包含：
- ✅ 总体测试概览（通过率、总耗时）
- 📊 各服务健康状态图表
- 🧪 详细测试结果（按类别分组）
- 📈 性能指标趋势
- ❌ 失败测试的详细错误日志
- 🔧 故障排除建议
- 📋 系统配置快照

## 故障处理

### 常见问题和解决方案

#### 服务启动失败
**症状**: 容器无法启动或反复重启
**诊断**:
```bash
docker-compose logs oauth2-backend
docker inspect oauth2-backend
```
**解决方案**:
1. 检查环境变量配置
2. 验证依赖服务状态
3. 检查端口冲突
4. 查看容器资源限制

#### 数据库连接失败
**症状**: 后端无法连接PostgreSQL
**诊断**:
```bash
docker exec oauth2-postgres psql -U oauth2_user -d oauth2_db -c "SELECT 1;"
docker network inspect oauth2-net
```
**解决方案**:
1. 验证数据库容器状态
2. 检查网络连接
3. 确认数据库初始化完成
4. 验证凭证配置

#### Redis连接问题
**症状**: 缓存操作失败
**诊断**:
```bash
docker exec oauth2-redis redis-cli -a redis_secret_pass ping
```
**解决方案**:
1. 检查Redis密码配置
2. 验证网络可达性
3. 确认Redis服务状态

#### OAuth2流程失败
**症状**: 授权或token获取失败
**诊断**:
```bash
curl -v http://localhost:5555/oauth2/authorize?response_type=code&client_id=vue-client
docker-compose logs oauth2-backend | grep -i error
```
**解决方案**:
1. 验证客户端配置
2. 检查重定向URI设置
3. 确认用户数据存在
4. 查看详细错误日志

## 输出格式

### HTML报告结构
- **概览部分**: 总体测试状态和关键指标
- **健康检查**: 各服务的实时状态
- **功能测试**: 按类别分组的测试结果
- **性能分析**: 响应时间和吞吐量数据
- **错误详情**: 失败测试的完整堆栈跟踪
- **建议部分**: 针对发现问题的具体解决建议

### 控制台输出
```bash
🐳 Docker集成测试开始...
✅ 环境准备完成
✅ 健康检查通过 (5/5服务)
⚠️ 后端测试: 9/10 通过
✅ 前端测试: 3/3 通过
📊 测试报告: ./test-results/docker-integration-test-report.html
🔍 失败详情请查看报告
```

## 性能基准

### 预期性能指标
- **健康检查**: < 2秒
- **授权流程**: < 500ms
- **token获取**: < 300ms
- **数据库查询**: < 100ms
- **Redis操作**: < 10ms

### 负载测试
- **并发用户**: 100个并发请求
- **响应时间**: P95 < 1秒
- **成功率**: > 99%

## 最佳实践

1. **定期测试**: 每次代码变更后运行
2. **环境隔离**: 使用专用的测试数据库
3. **数据清理**: 每次测试前清理旧数据
4. **日志收集**: 保存完整的测试日志
5. **性能监控**: 跟踪性能指标变化
6. **自动化集成**: 集成到CI/CD流程

## 注意事项

- 确保Docker服务正在运行
- 测试会修改数据库内容，使用专用测试环境
- 某些测试可能需要较长时间（5-10分钟）
- 确保端口5555、8080、5433、6380、9090未被占用
- 测试报告文件较大，确保有足够磁盘空间
