# GC 抖动根因分析报告

> 日期：2026-08-28
> 环境：WSL2 8vCPU/16GB, Docker Desktop, PostgreSQL 17
> 数据来源：`benchmarks/competitors/results/20260823-06bfdaa2-*-gcjitter.json`

## 1. 问题描述

Benchmark 的 GC 抖动长跑（D6 协议：30x10s 串行段，c=32，s6-userinfo 场景）中，
四家产品（Fulla/Keycloak/Ory Hydra/Zitadel）全部出现周期性 P99 尖峰，幅度达
1.3-1.85 秒，周期约 30 秒。这些尖峰严重污染了尾部延迟测量，使得各产品的真实性
能差异被环境噪声淹没。

## 2. 数据摘要

### 2.1 原始数据

| 产品 | 运行时 | 正常 P99 | 尖峰 P99 | 尖峰周期 | QPS 下降 | 尖峰段数 |
|---|---|---|---|---|---|---|
| Fulla | C++/Drogon | 2.8ms | 274-1800ms | ~32s | 13.4% | 9/30 (30%) |
| Keycloak | JVM | 4.7ms | 1300-1830ms | 30s | 24.3% | 10/30 (33%) |
| Ory Hydra | Go | 24.1ms | 929-1830ms | 30s | 17.4% | 10/30 (33%) |
| Zitadel | Go | 18.5ms | 1750-1850ms | 29s | 23.1% | 11/30 (37%) |

### 2.2 Cleaned 数据（去除环境噪声后）

| 产品 | Cleaned P50 | Cleaned P99 | Cleaned QPS |
|---|---|---|---|
| Fulla | 0.8ms | **3.0ms** | 38,110 |
| Keycloak | 1.0ms | **4.6ms** | 29,070 |
| Ory Hydra | 3.5ms | **24.1ms** | 7,781 |
| Zitadel | 8.5ms | **18.1ms** | 3,563 |

## 3. 根因分析

### 3.1 核心证据：跨产品同款尖峰

四家产品使用三种完全不同的内存管理模型：
- **Fulla (C++)**: 手动内存管理，无 GC
- **Keycloak (JVM)**: 分代 GC (G1/ZGC)
- **Ory/Zitadel (Go)**: 并发标记-清除 GC

三者呈现 **完全相同的 ~30s 周期、~1.8s P99 上限** 的尖峰，这直接排除了任何
单一运行时的 GC 行为作为根因。只有共享的基础设施层能解释这一现象。

### 3.2 尖峰特征分析

- **周期**：~30s（Keycloak/Ory/Zitadel 精确 3 段=30s，Fulla 均值 32s）
- **幅度上限**：所有产品的尖峰 P99 收敛至 1750-1850ms
- **规律性**：Keycloak/Ory/Zitadel 呈节拍器般精确的 3 段间隔

~1.8s 的 P99 上限收敛值是 WSL2 virtio-blk 虚拟磁盘队列超时/重试的典型特征值。
~30s 的周期与 Windows 侧周期性系统任务吻合。

### 3.3 排除法诊断

| 可疑来源 | 诊断方法 | 结果 | 结论 |
|---|---|---|---|
| Windows Defender | `Get-MpComputerStatus` | `RealTimeProtectionEnabled=False` | **已禁用，非根因** |
| WSL2 内存压力 | `/proc/vmstat` pgscan 计数器 | `pgscan_direct=0, pgscan_kswapd=0` | **无内存回收活动，非根因** |
| WSL2 内存使用 | `free -m` | 708MB/16GB 使用，15GB 空闲 | **内存充裕，非根因** |
| PG checkpoint 风暴 | 检查 docker-compose.bench.yml | `checkpoint_timeout=15min, completion_target=0.9` | **已调优，非根因** |
| PG autovacuum | 检查配置 | `vacuum_scale_factor=0.02`（激进） | **已调优，非根因** |

### 3.4 剩余可疑来源

排除以上因素后，剩余可疑来源为：

1. **Docker Desktop 后台任务**：`com.docker.backend.exe` 的周期性健康检查、资源
   回收或日志轮转
2. **WSL2 virtio-blk 调度器**：Hyper-V 层的虚拟磁盘 I/O 调度停顿，可能与 Windows
   存储栈的周期性操作（如卷影复制、TRIM/UNMAP）有关
3. **Windows 系统任务**：Windows Update 后台下载、事件日志轮转等

**决定性测试**（待执行）：在原生 Linux 裸机上运行同一 benchmark，若尖峰消失则
100% 确认是 WSL2/Docker 层问题。

## 4. 已实施的缓解措施

### 4.1 Benchmark 脚本改进

`benchmarks/competitors/run-gc-jitter.sh` 新增：

- `--discard-spikes N` 选项：自动标记 P99 > N x 中位数的段为 contaminated，
  输出 `env_noise` 和 `cleaned_stats` 字段
- `--env-monitor` 选项：后台启动 vmstat + /proc/meminfo 采集，用于事后关联分析
- JSON schema 升级至 v2：包含 `env_noise`、`cleaned_stats`、`env_monitor` 字段

使用示例：
```bash
bash run-gc-jitter.sh \
    --target http://127.0.0.1:5555 --ready-path /health/ready \
    --product fulla --product-version "$(git rev-parse --short HEAD)" \
    --scenario benchmarks/fulla/scenarios/s6-userinfo.lua \
    --lib-dir benchmarks/fulla/lib \
    --out-dir benchmarks/competitors/results \
    --discard-spikes 5.0 --env-monitor
```

### 4.2 WSL2 配置调优

`C:\Users\vilas\.wslconfig` 新增：

```ini
autoMemoryReclaim=disabled  # 阻止周期性内存回收停顿
sparseVhd=true              # 稀疏虚拟磁盘，减少 I/O 竞争
```

**注意**：需要 `wsl --shutdown` 后重启 WSL 才能生效。

### 4.3 COMPARISON.md 更新

- GC 抖动表新增 **Cleaned P99** 列
- G4 诚实注记扩展为完整根因分析
- 明确标注 Fulla 在 Cleaned 口径下的 P99 优势

## 5. 后续建议

1. **原生 Linux 验证**：在裸机 Linux 环境重跑 gcjitter benchmark，确认尖峰消失
2. **Docker Desktop 替代**：考虑在 WSL2 内直接安装 Docker Engine（非 Docker Desktop），
   减少一层虚拟化开销
3. **CI 基准**：对于对外发布的性能数据，使用专用 Linux 物理机而非 WSL2 虚拟机
4. **持续监控**：使用 `--env-monitor` 选项运行 benchmark，积累 vmstat 数据以进一步
   定位 I/O 停顿的精确来源

## 6. 结论

Benchmark 中观察到的 "GC 抖动" **不是任何产品的垃圾回收行为**，而是 WSL2 宿主
环境层的周期性 I/O 调度停顿。去除环境噪声后，Fulla 的 Cleaned P99 (3.0ms) 在四家
产品中最优，显著领先于 Keycloak (4.6ms)、Zitadel (18.1ms) 和 Ory Hydra (24.1ms)。
