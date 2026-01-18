---
description: 后端项目构建流程
---

# OAuth2Backend 构建流程

## 前置条件

- 已安装 Conan 2.x 包管理器
- 已安装 CMake 3.20+
- 已安装 MSVC 编译器 (Visual Studio 2022)

## 构建步骤

### 1. 停止正在运行的服务进程

// turbo

```powershell
taskkill /F /IM OAuth2Server.exe 2>$null; Write-Host "进程已清理"
```

### 2. 进入后端目录

// turbo

```powershell
cd d:\work\development\Repos\backend\drogon-plugin\OAuth2-plugin-example\OAuth2Backend
```

### 3. 执行构建脚本

**Debug 构建：**

```powershell
.\build.bat -debug
```

**Release 构建（默认）：**

```powershell
.\build.bat -release
```

### 4. 验证构建产物

// turbo

```powershell
Test-Path "build\Release\OAuth2Server.exe" -or Test-Path "build\Debug\OAuth2Server.exe"
```

### 5. 启动服务

```powershell
cd build\Release
.\OAuth2Server.exe
```

## 注意事项

- 构建前脚本会自动清理旧的 build 目录
- 构建完成后 config.json 会自动复制到对应的构建目录
- 如遇编译锁定文件问题，确保已停止所有相关进程
