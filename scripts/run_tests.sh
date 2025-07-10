#!/bin/bash
# 构建并运行单元测试

set -e  # 启用严格模式，遇到错误立即退出

# 获取脚本所在目录的绝对路径
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# 项目根目录是脚本目录的上一级
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

echo "脚本目录: $SCRIPT_DIR"
echo "项目根目录: $PROJECT_ROOT"

# 切换到项目根目录
cd "$PROJECT_ROOT" || { echo "无法切换到项目根目录: $PROJECT_ROOT"; exit 1; }

echo "当前工作目录: $(pwd)"
echo "目录内容："
ls -la

# 创建构建目录（如果不存在）
if [ ! -d "build" ]; then
  echo "创建构建目录..."
  mkdir build || { echo "创建构建目录失败"; exit 1; }
fi

# 进入构建目录
cd build || { echo "进入构建目录失败"; exit 2; }

# 输出当前目录内容用于调试
echo "构建目录内容："
ls -la

# 运行CMake配置
echo "运行CMake配置..."
cmake .. || { echo "CMake配置失败"; exit 3; }

# 构建测试项目
echo "构建测试项目..."
make || { echo "构建失败"; exit 4; }

# 运行测试
echo "运行FlowCoro综合测试..."
./tests/flowcoro_tests || { echo "综合测试运行失败"; exit 5; }

echo "所有测试通过！✅"