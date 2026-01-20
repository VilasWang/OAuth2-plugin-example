---
description: 重新生成 Drogon ORM 模型
---

# ORM 模型生成

## 前置条件

- PostgreSQL 数据库已启动
- 数据库表结构已创建
- 已安装 `drogon_ctl` 工具

## 1. 进入模型目录

// turbo

```powershell
cd d:\work\development\Repos\backend\drogon-plugin\OAuth2-plugin-example\OAuth2Backend\models
```

## 2. 生成 ORM 模型

```powershell
drogon_ctl create model .
```

## 3. 验证生成结果

// turbo

```powershell
Get-ChildItem *.h, *.cc | Select-Object Name, LastWriteTime
```

## 配置文件

模型生成配置位于 `models/model.json`：

```json
{
    "rdbms": "postgresql",
    "host": "127.0.0.1",
    "port": 5432,
    "dbname": "oauth_test",
    "user": "postgres",
    "tables": [
        "users",
        "oauth2_clients",
        "oauth2_codes",
        "oauth2_access_tokens",
        "oauth2_refresh_tokens"
    ]
}
```

## 注意事项

- ORM 生成的类**禁止手动修改**
- 如需变更，应修改数据库表结构后重新生成
- 生成后需要重新编译项目：`/build`
