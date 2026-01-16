# OAuth2 Plugin 集成指南

本文档介绍如何将本项目的 `OAuth2Plugin` 移植并集成到其他的 Drogon 应用程序中。

## 1. 文件依赖

本插件由以下核心文件组成，位于 `OAuth2Backend/plugins/` 目录：

1. **Plugin Interface & Core**:
    * `OAuth2Plugin.h` / `.cc`
2. **Storage Interface**:
    * `IOAuth2Storage.h`
3. **Storage Implementations**:
    * `MemoryOAuth2Storage.h` / `.cc`
    * `PostgresOAuth2Storage.h` / `.cc`
    * `RedisOAuth2Storage.h` / `.cc`

## 2. 集成步骤

### 第一步：复制文件

将上述 `plugins/` 目录下的所有文件复制到你目标项目的插件目录（例如 `src/plugins`）。

### 第二步：配置 CMake

在你的 `CMakeLists.txt` 中，确保这组源文件被包含在编译目标中。如果使用 `aux_source_directory` 自动扫描，只需确保文件在扫描路径内。

同时，确保你的项目已链接必要的库：

* `Drogon`
* `PostgreSQL` (`libpq` 或 Drogon 内置 ORM 支持)
* `Redis` (Drogon 内置 Redis 支持)
* `OpenSSL` (用于 SHA256 哈希)

### 第三步：注册 Plugin

在 `config.json` 的 `plugins` 列表中添加配置：

```json
{
    "plugins": [
        {
            "name": "OAuth2Plugin",
            "dependencies": [],
            "config": {
                "storage_type": "postgres",
                "postgres": {
                    "db_client_name": "default"
                },
                "redis": {
                    "client_name": "default"
                },
                // 仅 Memory 模式需要在此配置 client
                "clients": {} 
            }
        }
    ]
}
```

### 第四步：Controller 调用

在业务 Controller 中，通过 `drogon::app().getPlugin<OAuth2Plugin>()` 获取插件实例。

```cpp
auto plugin = drogon::app().getPlugin<OAuth2Plugin>();
if (!plugin) {
    // Handle error
}

// 示例：校验 Client
if (!plugin->validateClient(clientId, clientSecret)) {
    // ...
}
```

## 3. 注意事项

1. **数据库连接**：确保 `config.json` 中配置的 `db_client_name` 与 `db_clients` 中的名称一致。
2. **Redis 连接**：确保 `redis` 配置块中的 `client_name` 与 `redis_clients` 中的名称一致。
3. **安全性**：生产环境务必确保存储的 Client Secret 是经过加盐哈希的（SHA256）。

## 4. 扩展开发

如果需要支持新的存储后端（如 MySQL），只需继承 `IOAuth2Storage` 接口实现相应的类，并在 `OAuth2Plugin::initStorage` 中添加初始化逻辑即可。
