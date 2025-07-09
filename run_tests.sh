#!/bin/bash
# 构建并运行单元测试

set -e  # 启用严格模式，遇到错误立即退出

# 输出当前目录内容用于调试
echo "当前目录内容："
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
echo "运行测试..."
./flowcoro_tests || { echo "测试运行失败"; exit 5; }

echo "所有测试通过！"