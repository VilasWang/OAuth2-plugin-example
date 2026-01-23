---
description: Windows Docker Desktop 安装与初始化指南
---

# Docker Desktop for Windows 安装指南

## 1. 前置条件 (WSL 2)

Docker Desktop 在 Windows 上不仅更快，而且更稳定，推荐使用 WSL 2 (Windows Subsystem for Linux) 后端。

### 开启 WSL 功能

以 **管理员身份** 打开 PowerShell，运行：

```powershell
wsl --install
```

* 如果系统提示已安装，请确保更新到最新版。
* 安装完成后，**必须重启电脑**。

## 2. 下载与安装

1. 前往 [Docker 官网](https://www.docker.com/products/docker-desktop/) 下载 "Docker Desktop for Windows"。
2. 运行安装程序 (`Docker Desktop Installer.exe`)。
3. 在配置界面中，**务必勾选**：
    * `Use WSL 2 instead of Hyper-V` (推荐)
4. 等待安装完成，点击 "Close and restart"（可能需要注销或重启）。

## 3. 初始化与验证

1. 启动 **Docker Desktop** 应用。
2. 第一次运行时，接受服务条款（Accept Terms）。
3. 等待左下角的鲸鱼图标变为 **绿色 (Engine Running)**。
4. 打开 PowerShell，运行以下命令验证：

```powershell
docker --version
docker run hello-world
```

如果看到 "Hello from Docker!" 的输出，说明安装不仅成功，而且已经可以运行 Linux容器了。

## 4. 常见问题

* **WSL Kernel 版本过低**: 可能会弹出提示框，按提示访问微软链接下载 `wsl_update_x64.msi` 安装即可。
* **启用 Kubernetes**: 如果需要 K8s，可以在 Docker Desktop 设置 -> Kubernetes -> Enable Kubernetes 中开启（这一步会下载大量镜像，初次不建议开启，除非需要）。

## 5. 验证本项目构建

安装完成后，回到本项目根目录，运行此前提供的验证脚本：

```powershell
./verify-docker.bat
# 或者手动运行
docker build -t oauth2-server .
```
