---
name: code-review
description: 针对 Drogon 项目的自动化代码审查流程 (Format, Lint, Arch, Tests)
---

# Code Review Skill

用于对项目代码进行全自动化的质量检查，包括格式、风格、架构合规性和单元测试。

## Usage

```bash
/code-review [options] [files...]
```

### Options

- (default): 检查 Git 暂存区 (staged) 和工作区 (unstaged) 的变更文件
- `--all`: 全量检查项目中的所有源文件
- `--fix`: 【新】自动修复 Clang-Format 格式问题
- `file1 file2 ...`: 仅检查指定的文件

## Implementation

<step>
# Execute the main python script
import sys
import os
import subprocess

# Get arguments passed to the skill

args = "{{args}}".split()

# Locate the run.py script relative to this SKILL.md

skill_dir = os.path.dirname(os.path.abspath(__file__))
script_path = os.path.join(skill_dir, "scripts", "run.py")

# Execute the script with the same python interpreter

cmd = [sys.executable, script_path] + args
subprocess.run(cmd)
</step>
