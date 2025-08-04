# 表情符号清理工具

FlowCoro项目的表情符号一键清理工具，用于将代码和文档中的表情符号替换为空字符串，让项目更加严谨和专业。

## 工具说明

提供了两个版本的清理工具：

### 1. Python版本 (推荐)
- **文件**: `remove_emojis.py`
- **特点**: 功能完整，支持Unicode表情符号检测，处理更准确
- **依赖**: Python 3.6+

### 2. Bash版本
- **文件**: `remove_emojis.sh`
- **特点**: 无外部依赖，基于预定义表情符号列表
- **依赖**: Bash shell

## 快速使用

### 试运行模式（推荐先使用）

```bash
# Python版本 - 试运行
cd /home/caixuf/MyCode/flowcord
python3 scripts/remove_emojis.py

# Bash版本 - 试运行
cd /home/caixuf/MyCode/flowcord
./scripts/remove_emojis.sh
```

### 实际执行清理

```bash
# Python版本 - 实际执行
python3 scripts/remove_emojis.py --apply

# Bash版本 - 实际执行
./scripts/remove_emojis.sh --apply
```

### 执行清理并备份原文件

```bash
# Python版本 - 执行并备份
python3 scripts/remove_emojis.py --apply --backup

# Bash版本 - 执行并备份
./scripts/remove_emojis.sh --apply --backup
```

## 命令选项

### Python版本选项

```bash
python3 scripts/remove_emojis.py [选项]

选项:
  --dry-run 试运行模式，不修改文件（默认）
  --apply 实际执行修改（覆盖--dry-run）
  --backup 修改前备份原文件
  --directory DIR 指定要处理的目录（默认当前目录）
  --help 显示帮助信息
```

### Bash版本选项

```bash
./scripts/remove_emojis.sh [选项]

选项:
  --apply 实际执行清理（默认为试运行模式）
  --backup 修改前备份原文件
  --verbose, -v 显示详细输出
  --help, -h 显示帮助信息
```

## 处理的文件类型

脚本会处理以下类型的文件：

- **C++源码**: `*.cpp`, `*.hpp`, `*.h`, `*.c`, `*.cc`, `*.cxx`
- **文档文件**: `*.md`, `*.txt`, `*.rst`
- **构建文件**: `*.cmake`, `CMakeLists.txt`
- **脚本文件**: `*.py`, `*.sh`, `*.bash`
- **配置文件**: `*.json`, `*.yaml`, `*.yml`
- **日志文件**: `*.log`

## 处理的表情符号

### Python版本
- 支持完整的Unicode表情符号范围
- 自动检测各种类型的表情符号
- 包含FlowCoro项目中常见的表情符号

### Bash版本
- 基于预定义列表的常见表情符号：
  -
  -
  -
  - 等等...

## 安全特性

1. **试运行模式**: 默认为试运行，先预览会进行哪些修改
2. **备份功能**: 可选择在修改前自动备份原文件
3. **确认提示**: 实际执行前需要用户确认
4. **跳过特殊目录**: 自动跳过 `.git`, `build`, `.cache` 等目录

## 使用示例

### 场景1: 首次使用，想看看会处理哪些文件

```bash
cd /home/caixuf/MyCode/flowcord
python3 scripts/remove_emojis.py --verbose
```

### 场景2: 确认要清理，并且要备份

```bash
cd /home/caixuf/MyCode/flowcord
python3 scripts/remove_emojis.py --apply --backup
```

### 场景3: 只处理特定目录

```bash
cd /home/caixuf/MyCode/flowcord
python3 scripts/remove_emojis.py --apply --directory ./docs
```

### 场景4: 使用Bash版本进行快速清理

```bash
cd /home/caixuf/MyCode/flowcord
./scripts/remove_emojis.sh --apply --verbose
```

## 输出示例

```
 FlowCoro 表情符号清理工具
==============================
目标目录: /home/caixuf/MyCode/flowcord
运行模式: 试运行
备份选项: 否

 开始处理目录: /home/caixuf/MyCode/flowcord
============================================================
处理文件: /home/caixuf/MyCode/flowcord/README.md
   发现 25 个表情符号
   [试运行] 将清理 25 个表情符号

处理文件: /home/caixuf/MyCode/flowcord/docs/USAGE.md
   发现 12 个表情符号
   [试运行] 将清理 12 个表情符号

============================================================
 处理摘要
============================================================
总处理文件数: 45
修改文件数: 8
总替换数量: 89

 这是试运行模式，实际文件未被修改
 使用 --apply 参数来实际执行修改
```

## 注意事项

1. **建议先试运行**: 总是先使用默认的试运行模式查看会有哪些修改
2. **重要文件备份**: 对重要文件建议使用 `--backup` 选项
3. **编码问题**: Python版本会自动尝试UTF-8和GB2312编码
4. **权限检查**: 确保脚本有足够的文件读写权限

## 恢复文件

如果使用了 `--backup` 选项，可以这样恢复文件：

```bash
# 恢复单个文件
mv file.txt.bak file.txt

# 批量恢复所有备份文件
find . -name "*.bak" -exec sh -c 'mv "$1" "${1%.bak}"' _ {} \;
```
